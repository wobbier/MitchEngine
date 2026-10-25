#include "VFS/Pak.h"
#include "VFS/VFS.h"
#include "CLog.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace
{
    std::string NormalizeEntryPath( const std::string& InPath )
    {
        std::string path = InPath;
        std::replace( path.begin(), path.end(), '\\', '/' );
        while( !path.empty() && path.front() == '/' )
        {
            path.erase( path.begin() );
        }
        return path;
    }


    template <typename T>
    void WritePod( std::ofstream& InStream, const T& InValue )
    {
        InStream.write( reinterpret_cast<const char*>( &InValue ), sizeof( T ) );
    }


    template <typename T>
    bool ReadPod( std::ifstream& InStream, T& OutValue )
    {
        InStream.read( reinterpret_cast<char*>( &OutValue ), sizeof( T ) );
        return InStream.gcount() == sizeof( T );
    }
}


namespace Mpak
{
    bool Write( const std::string& InPakPath, const EntryProvider& InProvider )
    {
        std::error_code ec;
        const std::filesystem::path pakPath( InPakPath );
        if( pakPath.has_parent_path() )
        {
            std::filesystem::create_directories( pakPath.parent_path(), ec );
        }
        std::ofstream pak( InPakPath, std::ios::binary | std::ios::trunc );
        if( !pak )
        {
            return false;
        }

        Header header;
        WritePod( pak, header );

        // Blobs stream in provider order; the table collects behind them.
        std::vector<std::pair<std::string, Entry>> entries;
        InProvider( [&pak, &entries]( const std::string& InPath, const void* InBytes, uint64_t InSize ) {
            Entry entry;
            entry.Offset = static_cast<uint64_t>( pak.tellp() );
            entry.Size = InSize;
            if( InSize > 0 )
            {
                pak.write( static_cast<const char*>( InBytes ), static_cast<std::streamsize>( InSize ) );
            }
            entries.emplace_back( NormalizeEntryPath( InPath ), entry );
        } );

        header.TableOffset = static_cast<uint64_t>( pak.tellp() );
        header.EntryCount = entries.size();
        for( const auto& [path, entry] : entries )
        {
            const uint16_t length = static_cast<uint16_t>( std::min<size_t>( path.size(), UINT16_MAX ) );
            WritePod( pak, length );
            pak.write( path.data(), length );
            WritePod( pak, entry.Offset );
            WritePod( pak, entry.Size );
        }

        pak.seekp( 0 );
        WritePod( pak, header );
        pak.close();
        return static_cast<bool>( pak );
    }


    bool WriteFromVfs( const std::string& InPakPath, const std::vector<std::string>& InLogicalRoots )
    {
        // Collect first: Write streams while the VFS reads, and entries key by logical path, so
        // the mounted overlay (game files shadowing engine files) bakes in here.
        std::vector<std::string> files;
        for( const std::string& root : InLogicalRoots )
        {
            const VfsResolved resolved = VFS::Get().Resolve( root );
            if( resolved.Exists && resolved.IsFile )
            {
                files.push_back( root );
                continue;
            }
            VFS::Get().Enumerate( root, true, [&files]( const std::string& InLogicalPath, bool InIsDirectory ) {
                if( !InIsDirectory )
                {
                    files.push_back( InLogicalPath );
                }
            } );
        }
        std::sort( files.begin(), files.end() );
        files.erase( std::unique( files.begin(), files.end() ), files.end() );

        size_t failed = 0;
        const bool written = Write( InPakPath, [&files, &failed]( const EntrySink& InSink ) {
            std::vector<uint8_t> bytes;
            for( const std::string& file : files )
            {
                bytes.clear();
                if( VFS::Get().ReadBytes( file, bytes ) )
                {
                    InSink( file, bytes.data(), bytes.size() );
                }
                else
                {
                    ++failed;
                }
            }
        } );
        if( failed > 0 )
        {
            CLog::Log( CLog::LogType::Warning, "Mpak: " + std::to_string( failed ) + " file(s) could not be read and were skipped" );
        }
        if( written )
        {
            CLog::Log( CLog::LogType::Info, "Mpak: wrote " + InPakPath + " (" + std::to_string( files.size() - failed ) + " entries)" );
        }
        return written;
    }
}


SharedPtr<PakMount> PakMount::Open( const std::string& InPakPath )
{
    std::ifstream pak( InPakPath, std::ios::binary );
    if( !pak )
    {
        return nullptr;
    }
    Mpak::Header header;
    if( !ReadPod( pak, header )
        || std::memcmp( header.Magic, Mpak::Header::kMagic, sizeof( header.Magic ) ) != 0
        || header.Version != Mpak::Header::kVersion )
    {
        return nullptr;
    }
    pak.seekg( static_cast<std::streamoff>( header.TableOffset ) );
    SharedPtr<PakMount> mount( new PakMount() );
    std::error_code ec;
    mount->m_pakPath = std::filesystem::absolute( InPakPath, ec ).lexically_normal().generic_string();
    for( uint64_t i = 0; i < header.EntryCount; ++i )
    {
        uint16_t length = 0;
        if( !ReadPod( pak, length ) )
        {
            return nullptr;
        }
        std::string path( length, '\0' );
        pak.read( path.data(), length );
        Mpak::Entry entry;
        if( pak.gcount() != length || !ReadPod( pak, entry.Offset ) || !ReadPod( pak, entry.Size ) )
        {
            return nullptr;
        }
        mount->m_entries.emplace( std::move( path ), entry );
    }
    return mount;
}


VfsStat PakMount::Stat( const std::string& InRelativePath ) const
{
    VfsStat stat;
    const auto entry = m_entries.find( InRelativePath );
    if( entry != m_entries.end() )
    {
        stat.Exists = true;
        stat.IsFile = true;
        return stat;
    }
    // A directory exists when any entry lives under it.
    const std::string prefix = InRelativePath.empty() ? std::string() : InRelativePath + "/";
    const auto below = m_entries.lower_bound( prefix );
    if( InRelativePath.empty() ? !m_entries.empty() : ( below != m_entries.end() && below->first.rfind( prefix, 0 ) == 0 ) )
    {
        stat.Exists = true;
        stat.IsFile = false;
    }
    return stat;
}


std::string PakMount::GetPhysicalPath( const std::string& InRelativePath ) const
{
    // Entries aren't files on disk; readers go through ReadText/ReadBytes/GetStreamWindow.
    ( void )InRelativePath;
    return {};
}


bool PakMount::ReadText( const std::string& InRelativePath, std::string& OutText ) const
{
    std::vector<uint8_t> bytes;
    if( !ReadBytes( InRelativePath, bytes ) )
    {
        return false;
    }
    OutText.assign( reinterpret_cast<const char*>( bytes.data() ), bytes.size() );
    return true;
}


bool PakMount::ReadBytes( const std::string& InRelativePath, std::vector<uint8_t>& OutBytes ) const
{
    const auto entry = m_entries.find( InRelativePath );
    if( entry == m_entries.end() )
    {
        return false;
    }
    // A fresh handle per read keeps loader threads independent.
    std::ifstream pak( m_pakPath, std::ios::binary );
    if( !pak )
    {
        return false;
    }
    pak.seekg( static_cast<std::streamoff>( entry->second.Offset ) );
    OutBytes.resize( static_cast<size_t>( entry->second.Size ) );
    if( entry->second.Size > 0 )
    {
        pak.read( reinterpret_cast<char*>( OutBytes.data() ), static_cast<std::streamsize>( entry->second.Size ) );
        return pak.gcount() == static_cast<std::streamsize>( entry->second.Size );
    }
    return true;
}


bool PakMount::WriteText( const std::string& InRelativePath, const std::string& InText ) const
{
    ( void )InRelativePath;
    ( void )InText;
    return false;
}


void PakMount::Enumerate( const std::string& InRelativeDirectory, bool InRecursive,
                          const std::function<void( const std::string& InRelativePath, bool InIsDirectory )>& InVisitor ) const
{
    const std::string prefix = InRelativeDirectory.empty() ? std::string() : InRelativeDirectory + "/";
    std::vector<std::string> directories;
    for( auto it = m_entries.lower_bound( prefix ); it != m_entries.end(); ++it )
    {
        const std::string& path = it->first;
        if( path.rfind( prefix, 0 ) != 0 )
        {
            break;
        }
        const std::string below = path.substr( prefix.size() );
        const size_t slash = below.find( '/' );
        if( slash == std::string::npos )
        {
            InVisitor( path, false );
            continue;
        }
        // Synthesize each directory once (entries are sorted, so repeats are adjacent).
        const std::string directory = prefix + below.substr( 0, slash );
        if( directories.empty() || directories.back() != directory )
        {
            directories.push_back( directory );
            InVisitor( directory, true );
        }
        if( InRecursive )
        {
            InVisitor( path, false );
        }
    }
}


bool PakMount::GetStreamWindow( const std::string& InRelativePath, VfsStreamWindow& OutWindow ) const
{
    const auto entry = m_entries.find( InRelativePath );
    if( entry == m_entries.end() )
    {
        return false;
    }
    OutWindow.PhysicalFile = m_pakPath;
    OutWindow.Offset = entry->second.Offset;
    OutWindow.Size = entry->second.Size;
    return true;
}
