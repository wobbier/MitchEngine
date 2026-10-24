#include "VFS/VFS.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

#include "CLog.h"
#include "Path.h"

namespace
{
    // The historical local-path rule: everything from the last "Assets" on.
    std::string TrimAtAssets( const std::string& InPath )
    {
        const size_t pos = InPath.rfind( "Assets" );
        return pos == std::string::npos ? InPath : InPath.substr( pos );
    }


    bool ReadPhysicalText( const std::string& InPhysicalPath, std::string& OutText )
    {
        // Text mode on purpose: File::Read's historical behavior.
        std::ifstream stream( InPhysicalPath, std::ios::in );
        if( !stream )
        {
            return false;
        }
        OutText.assign( ( std::istreambuf_iterator<char>( stream ) ), ( std::istreambuf_iterator<char>() ) );
        return true;
    }


    bool WritePhysicalText( const std::string& InPhysicalPath, const std::string& InText )
    {
        std::error_code ec;
        const std::filesystem::path parent = std::filesystem::path( InPhysicalPath ).parent_path();
        if( !parent.empty() )
        {
            std::filesystem::create_directories( parent, ec );
        }
        std::ofstream stream( InPhysicalPath );
        if( !stream )
        {
            return false;
        }
        stream << InText;
        return stream.good();
    }
}

bool VfsFileStream::Open( const Path& InPath )
{
    Close();
    m_stream.open( InPath.FullPath, std::ios::in | std::ios::binary | std::ios::ate );
    if( !m_stream )
    {
        return false;
    }
    m_size = static_cast<uint64_t>( m_stream.tellg() );
    m_stream.seekg( 0 );
    m_open = true;
    return true;
}


void VfsFileStream::Close()
{
    if( m_open )
    {
        m_stream.close();
        m_stream.clear();
        m_open = false;
        m_size = 0;
    }
}


size_t VfsFileStream::Read( void* OutBuffer, size_t InSize )
{
    if( !m_open || InSize == 0 )
    {
        return 0;
    }
    m_stream.read( static_cast<char*>( OutBuffer ), static_cast<std::streamsize>( InSize ) );
    const std::streamsize count = m_stream.gcount();
    if( m_stream.eof() )
    {
        m_stream.clear();
    }
    return static_cast<size_t>( count );
}


bool VfsFileStream::Seek( uint64_t InPosition )
{
    if( !m_open )
    {
        return false;
    }
    m_stream.clear();
    m_stream.seekg( static_cast<std::streamoff>( InPosition ) );
    return static_cast<bool>( m_stream );
}


VFS::VFS()
{
    InstallDefaults();
}


VFS& VFS::Get()
{
    static VFS instance;
    return instance;
}


std::string VFS::GetScheme( const std::string& InLogicalPath )
{
    const size_t pos = InLogicalPath.find( "://" );
    if( pos == std::string::npos || pos == 0 )
    {
        return {};
    }
    for( size_t i = 0; i < pos; ++i )
    {
        const char c = InLogicalPath[i];
        const bool schemeChar = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '+' || c == '-' || c == '.';
        if( !schemeChar )
        {
            return {};
        }
    }
    return InLogicalPath.substr( 0, pos + 3 );
}


void VFS::InstallDefaults()
{
    // The working directory is fixed for the life of the process (Path has relied on this
    // forever), so the default roots are pinned once.
    std::error_code ec;
    const std::string cwd = std::filesystem::current_path( ec ).generic_string();

    std::vector<VfsMount> mounts;
    mounts.push_back( { "", MakeShared<DirectoryMount>( cwd ), 100, true, "" } );
    mounts.push_back( { "", MakeShared<DirectoryMount>( cwd + "/Engine" ), 0, true, "Engine/" } );
    // The dev default; shipped builds remount this at the OS pref dir in Engine::Init.
    mounts.push_back( { kUserScheme, MakeShared<DirectoryMount>( cwd + "/.tmp/User" ), 0, true, "" } );
    Swap( std::move( mounts ) );
}


void VFS::Swap( std::vector<VfsMount> InMounts )
{
    // Highest priority first; stable, so the first mounted wins ties.
    std::stable_sort( InMounts.begin(), InMounts.end(), []( const VfsMount& InA, const VfsMount& InB )
    {
        return InA.Priority > InB.Priority;
    } );
    SharedPtr<const MountTable> table = MakeShared<const MountTable>( MountTable { std::move( InMounts ) } );
    std::lock_guard<std::mutex> lock( m_mutex );
    m_table = std::move( table );
}


SharedPtr<const MountTable> VFS::Snapshot() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    return m_table;
}


void VFS::Mount( const std::string& InPrefix, const SharedPtr<IMountSource>& InSource, int InPriority, bool InWritable, const std::string& InDisplayPrefix )
{
    SharedPtr<const MountTable> current = Snapshot();
    std::vector<VfsMount> mounts = current->Mounts;
    mounts.push_back( { InPrefix, InSource, InPriority, InWritable, InDisplayPrefix } );
    Swap( std::move( mounts ) );
}


void VFS::MountDirectory( const std::string& InPrefix, const std::string& InDirectory, int InPriority, bool InWritable, const std::string& InDisplayPrefix )
{
    Mount( InPrefix, MakeShared<DirectoryMount>( InDirectory ), InPriority, InWritable, InDisplayPrefix );
}


void VFS::UnmountAll( const std::string& InPrefix )
{
    SharedPtr<const MountTable> current = Snapshot();
    std::vector<VfsMount> mounts;
    mounts.reserve( current->Mounts.size() );
    for( const VfsMount& mount : current->Mounts )
    {
        if( mount.Prefix != InPrefix )
        {
            mounts.push_back( mount );
        }
    }
    Swap( std::move( mounts ) );
}


void VFS::SetUserRoot( const std::string& InDirectory )
{
    UnmountAll( kUserScheme );
    MountDirectory( kUserScheme, InDirectory, 0, true, "" );
}


std::string VFS::GetUserRoot() const
{
    SharedPtr<const MountTable> table = Snapshot();
    for( const VfsMount& mount : table->Mounts )
    {
        if( mount.Prefix == kUserScheme )
        {
            return mount.Source->GetPhysicalPath( "" );
        }
    }
    return {};
}


void VFS::ResetToDefaults()
{
    InstallDefaults();
}


VfsResolved VFS::Resolve( const std::string& InLogicalPath, bool InPrimaryOnly ) const
{
    SharedPtr<const MountTable> table = Snapshot();

    std::string scheme = GetScheme( InLogicalPath );
    bool haveSchemeMount = false;
    if( !scheme.empty() )
    {
        for( const VfsMount& mount : table->Mounts )
        {
            if( mount.Prefix == scheme )
            {
                haveSchemeMount = true;
                break;
            }
        }
        if( !haveSchemeMount )
        {
            // An unknown scheme resolves like any relative path always has: against the roots.
            scheme.clear();
        }
    }

    VfsResolved result;
    bool haveWriteTarget = false;
    for( const VfsMount& mount : table->Mounts )
    {
        const bool matches = scheme.empty() ? mount.Prefix.empty() : mount.Prefix == scheme;
        if( !matches )
        {
            continue;
        }

        const std::string relative = InLogicalPath.substr( mount.Prefix.size() );
        if( !haveWriteTarget )
        {
            // The highest-priority mount is where the path lands when nothing has it yet.
            haveWriteTarget = true;
            result.PhysicalPath = mount.Source->GetPhysicalPath( relative );
            result.DisplayPath = mount.DisplayPrefix + TrimAtAssets( relative );
            result.MountPrefix = mount.Prefix;
            result.Writable = mount.Writable;
            result.Source = mount.Source;
            result.RelativePath = relative;
            result.Resolved = true;
        }

        const VfsStat stat = mount.Source->Stat( relative );
        if( stat.Exists )
        {
            result.PhysicalPath = mount.Source->GetPhysicalPath( relative );
            result.DisplayPath = mount.DisplayPrefix + TrimAtAssets( relative );
            result.MountPrefix = mount.Prefix;
            result.Writable = mount.Writable;
            result.Source = mount.Source;
            result.RelativePath = relative;
            result.Exists = true;
            result.IsFile = stat.IsFile;
            return result;
        }

        if( InPrimaryOnly )
        {
            break;
        }
    }

    return result;
}


bool VFS::ReadText( const Path& InPath, std::string& OutText ) const
{
    return ReadPhysicalText( InPath.FullPath, OutText );
}


bool VFS::ReadBytes( const Path& InPath, std::vector<uint8_t>& OutBytes ) const
{
    std::ifstream stream( InPath.FullPath, std::ios::in | std::ios::binary );
    if( !stream )
    {
        return false;
    }
    stream.seekg( 0, std::ios::end );
    const std::streamoff size = stream.tellg();
    stream.seekg( 0, std::ios::beg );
    OutBytes.resize( size > 0 ? static_cast<size_t>( size ) : 0 );
    if( !OutBytes.empty() )
    {
        stream.read( reinterpret_cast<char*>( OutBytes.data() ), OutBytes.size() );
    }
    return stream.good() || stream.eof();
}


bool VFS::WriteText( const Path& InPath, const std::string& InText ) const
{
    return WritePhysicalText( InPath.FullPath, InText );
}


bool VFS::ReadText( const std::string& InLogicalPath, std::string& OutText ) const
{
    const VfsResolved resolved = Resolve( InLogicalPath );
    if( !resolved.Exists || !resolved.Source )
    {
        return false;
    }
    return resolved.Source->ReadText( resolved.RelativePath, OutText );
}


bool VFS::ReadBytes( const std::string& InLogicalPath, std::vector<uint8_t>& OutBytes ) const
{
    const VfsResolved resolved = Resolve( InLogicalPath );
    if( !resolved.Exists || !resolved.Source )
    {
        return false;
    }
    return resolved.Source->ReadBytes( resolved.RelativePath, OutBytes );
}


bool VFS::WriteText( const std::string& InLogicalPath, const std::string& InText ) const
{
    SharedPtr<const MountTable> table = Snapshot();

    std::string scheme = GetScheme( InLogicalPath );
    if( !scheme.empty() )
    {
        bool haveSchemeMount = false;
        for( const VfsMount& mount : table->Mounts )
        {
            haveSchemeMount |= mount.Prefix == scheme;
        }
        if( !haveSchemeMount )
        {
            scheme.clear();
        }
    }

    for( const VfsMount& mount : table->Mounts )
    {
        const bool matches = scheme.empty() ? mount.Prefix.empty() : mount.Prefix == scheme;
        if( !matches || !mount.Writable )
        {
            continue;
        }
        return mount.Source->WriteText( InLogicalPath.substr( mount.Prefix.size() ), InText );
    }

    YIKES( "VFS: no writable mount serves " + InLogicalPath );
    return false;
}


void VFS::Enumerate( const std::string& InLogicalDirectory, bool InRecursive,
                     const std::function<void( const std::string& InLogicalPath, bool InIsDirectory )>& InVisitor ) const
{
    SharedPtr<const MountTable> table = Snapshot();

    std::string scheme = GetScheme( InLogicalDirectory );
    bool haveSchemeMount = false;
    if( !scheme.empty() )
    {
        for( const VfsMount& mount : table->Mounts )
        {
            haveSchemeMount |= mount.Prefix == scheme;
        }
        if( !haveSchemeMount )
        {
            scheme.clear();
        }
    }

    // Higher-priority mounts fill the map first and win duplicates; the map keeps the visit sorted.
    std::map<std::string, bool> merged;
    for( const VfsMount& mount : table->Mounts )
    {
        const bool matches = scheme.empty() ? mount.Prefix.empty() : mount.Prefix == scheme;
        if( !matches )
        {
            continue;
        }
        mount.Source->Enumerate( InLogicalDirectory.substr( mount.Prefix.size() ), InRecursive,
                                 [&]( const std::string& InRelativePath, bool InIsDirectory )
        {
            merged.emplace( mount.Prefix + InRelativePath, InIsDirectory );
        } );
    }

    for( const auto& [logicalPath, isDirectory] : merged )
    {
        InVisitor( logicalPath, isDirectory );
    }
}
