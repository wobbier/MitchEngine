#include "VFS/VFS.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

#include "CLog.h"
#include "Path.h"
#include "VFS/Pak.h"

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


    // A folder prefix ("Engine/") rather than a scheme ("user://") or the overlay roots ("").
    bool IsFolderPrefix( const std::string& InPrefix )
    {
        return !InPrefix.empty() && InPrefix.back() == '/' && VFS::GetScheme( InPrefix ).empty();
    }


    // Does InMount serve InLogicalPath? Scheme mounts serve their scheme, the overlay roots every
    // path without one, and folder-prefix mounts the scheme-less paths spelled under them (the
    // folder itself included: "Engine" for "Engine/").
    bool Serves( const VfsMount& InMount, const std::string& InScheme, const std::string& InLogicalPath )
    {
        if( !InScheme.empty() )
        {
            return InMount.Prefix == InScheme;
        }
        if( InMount.Prefix.empty() )
        {
            return true;
        }
        if( !IsFolderPrefix( InMount.Prefix ) )
        {
            return false;
        }
        return InLogicalPath.compare( 0, InMount.Prefix.size(), InMount.Prefix ) == 0
            || InLogicalPath.compare( 0, std::string::npos, InMount.Prefix, 0, InMount.Prefix.size() - 1 ) == 0;
    }


    // InLogicalPath relative to a mount that Serves it.
    std::string RelativeTo( const VfsMount& InMount, const std::string& InLogicalPath )
    {
        return InLogicalPath.size() > InMount.Prefix.size() ? InLogicalPath.substr( InMount.Prefix.size() ) : std::string();
    }


    // InLogicalPath's scheme when a mount serves it; empty otherwise, since an unknown scheme
    // resolves like any relative path always has: against the roots.
    std::string ServedScheme( const MountTable& InTable, const std::string& InLogicalPath )
    {
        const std::string scheme = VFS::GetScheme( InLogicalPath );
        if( scheme.empty() )
        {
            return scheme;
        }
        for( const VfsMount& mount : InTable.Mounts )
        {
            if( mount.Prefix == scheme )
            {
                return scheme;
            }
        }
        return {};
    }
}

bool VfsFileStream::Open( const Path& InPath )
{
    Close();
    // A window through the answering mount (a pak entry's slice, or a whole file); Paths
    // outside every mount (absolute .tmp artifacts) fall back to the physical file whole.
    VfsStreamWindow window;
    const VfsResolved resolved = VFS::Get().Resolve( InPath.GetLocalPathString() );
    const bool throughMount = !VFS::Get().NamesOtherLooseFile( InPath ) && resolved.Exists && resolved.Source
        && resolved.Source->GetStreamWindow( resolved.RelativePath, window );
    if( !throughMount )
    {
        window.PhysicalFile = InPath.FullPath;
        window.Offset = 0;
        std::error_code ec;
        window.Size = std::filesystem::file_size( InPath.FullPath, ec );
        if( ec )
        {
            return false;
        }
    }
    m_stream.open( window.PhysicalFile, std::ios::in | std::ios::binary );
    if( !m_stream )
    {
        return false;
    }
    m_begin = window.Offset;
    m_size = window.Size;
    m_cursor = 0;
    m_stream.seekg( static_cast<std::streamoff>( m_begin ) );
    m_open = static_cast<bool>( m_stream );
    if( m_open && throughMount )
    {
        VFS::Get().NoteRead( resolved );
    }
    return m_open;
}


void VfsFileStream::Close()
{
    if( m_open )
    {
        m_stream.close();
        m_stream.clear();
        m_open = false;
        m_begin = 0;
        m_cursor = 0;
        m_size = 0;
    }
}


size_t VfsFileStream::Read( void* OutBuffer, size_t InSize )
{
    if( !m_open || InSize == 0 || m_cursor >= m_size )
    {
        return 0;
    }
    const uint64_t remaining = m_size - m_cursor;
    const size_t toRead = static_cast<size_t>( std::min<uint64_t>( InSize, remaining ) );
    m_stream.read( static_cast<char*>( OutBuffer ), static_cast<std::streamsize>( toRead ) );
    const std::streamsize count = m_stream.gcount();
    if( m_stream.eof() )
    {
        m_stream.clear();
    }
    m_cursor += static_cast<uint64_t>( count );
    return static_cast<size_t>( count );
}


bool VfsFileStream::Seek( uint64_t InPosition )
{
    if( !m_open || InPosition > m_size )
    {
        return false;
    }
    m_stream.clear();
    m_stream.seekg( static_cast<std::streamoff>( m_begin + InPosition ) );
    if( !m_stream )
    {
        return false;
    }
    m_cursor = InPosition;
    return true;
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
    std::string gameRoot;
    std::string engineRoot;
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if( m_gameRoot.empty() )
        {
            // The working directory is fixed for the life of the process (Path has relied on
            // this forever; Project changes into the project before anything resolves).
            std::error_code ec;
            m_gameRoot = std::filesystem::current_path( ec ).generic_string();
            m_engineRoot = m_gameRoot + "/Engine";
        }
        gameRoot = m_gameRoot;
        engineRoot = m_engineRoot;
    }

    std::vector<VfsMount> mounts;
    SharedPtr<DirectoryMount> engine = MakeShared<DirectoryMount>( engineRoot );
    mounts.push_back( { "", MakeShared<DirectoryMount>( gameRoot ), 100, true, "" } );
    mounts.push_back( { "", engine, 0, true, kEnginePrefix } );
    if( engineRoot != gameRoot + "/Engine" )
    {
        // "Engine/..." names the engine's files whether or not the engine sits in the project;
        // above the game root, so primary-only lookups (Path's Raw flag) find it first.
        mounts.push_back( { kEnginePrefix, engine, 101, true, kEnginePrefix } );
    }
    // The dev default; shipped builds remount this at the OS pref dir in Engine::Init.
    mounts.push_back( { kUserScheme, MakeShared<DirectoryMount>( gameRoot + "/.tmp/User" ), 0, true, "" } );
    Swap( std::move( mounts ) );
}


void VFS::SetDefaultRoots( const std::string& InGameRoot, const std::string& InEngineRoot )
{
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_gameRoot = InGameRoot;
        m_engineRoot = InEngineRoot;
    }
    InstallDefaults();
}


std::string VFS::GetGameRoot() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    return m_gameRoot;
}


std::string VFS::GetEngineRoot() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    return m_engineRoot;
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


bool VFS::MountPak( const std::string& InPrefix, const std::string& InPakPath, int InPriority, const std::string& InDisplayPrefix )
{
    SharedPtr<PakMount> pak = PakMount::Open( InPakPath );
    if( pak == nullptr )
    {
        CLog::Log( CLog::LogType::Error, "VFS: can't mount pak " + InPakPath );
        return false;
    }
    CLog::Log( CLog::LogType::Info, "VFS: mounted pak " + InPakPath + " (" + std::to_string( pak->GetEntryCount() ) + " entries)" );
    Mount( InPrefix, pak, InPriority, false, InDisplayPrefix );
    return true;
}


void VFS::Unmount( const SharedPtr<IMountSource>& InSource )
{
    SharedPtr<const MountTable> current = Snapshot();
    std::vector<VfsMount> mounts;
    mounts.reserve( current->Mounts.size() );
    for( const VfsMount& mount : current->Mounts )
    {
        if( mount.Source != InSource )
        {
            mounts.push_back( mount );
        }
    }
    Swap( std::move( mounts ) );
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
    const std::string scheme = ServedScheme( *table, InLogicalPath );

    VfsResolved result;
    bool haveWriteTarget = false;
    for( const VfsMount& mount : table->Mounts )
    {
        if( !Serves( mount, scheme, InLogicalPath ) )
        {
            continue;
        }

        const std::string relative = RelativeTo( mount, InLogicalPath );
        if( !haveWriteTarget )
        {
            // Where the path lands when nothing has it yet: the highest-priority mount that
            // yields a physical location. Paks serve bytes but host no files, so they answer
            // the prefix without claiming the landing.
            const std::string physical = mount.Source->GetPhysicalPath( relative );
            if( !physical.empty() )
            {
                haveWriteTarget = true;
                result.PhysicalPath = physical;
                result.DisplayPath = mount.DisplayPrefix + TrimAtAssets( relative );
                result.MountPrefix = mount.Prefix;
                result.Writable = mount.Writable;
                result.Source = mount.Source;
                result.RelativePath = relative;
                result.Resolved = true;
            }
            else if( !result.Resolved )
            {
                result.DisplayPath = mount.DisplayPrefix + TrimAtAssets( relative );
                result.MountPrefix = mount.Prefix;
                result.Source = mount.Source;
                result.RelativePath = relative;
                result.Resolved = true;
            }
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


bool VFS::NamesOtherLooseFile( const Path& InPath ) const
{
    // A Path built from an absolute file names that file, but its local path is only the tail
    // from "Assets" on (an engine file's loses its "Engine/"). Resolving that through the overlay
    // can land on a different loose file with the same tail, like a stale game-side .meta beside
    // no source shadowing the engine's (ME-303). Pak hits have no physical path and stay
    // authoritative.
    if( InPath.FullPath.empty() || !InPath.Exists || !InPath.IsFile )
    {
        return false;
    }
    const VfsResolved resolved = Resolve( InPath.GetLocalPathString() );
    if( !resolved.Exists || resolved.PhysicalPath.empty() || resolved.PhysicalPath == InPath.FullPath )
    {
        return false;
    }
    std::error_code error;
    return !std::filesystem::equivalent( resolved.PhysicalPath, InPath.FullPath, error ) && !error;
}


bool VFS::ReadText( const Path& InPath, std::string& OutText ) const
{
    // The mounts are authoritative (a pak can shadow loose files); Paths outside every mount
    // (absolute .tmp artifacts) miss resolution and fall back to their physical file, and so do
    // Paths whose local path resolves to some other loose file.
    if( !NamesOtherLooseFile( InPath ) && ReadText( InPath.GetLocalPathString(), OutText ) )
    {
        return true;
    }
    return !InPath.FullPath.empty() && ReadPhysicalText( InPath.FullPath, OutText );
}


bool VFS::ReadBytes( const Path& InPath, std::vector<uint8_t>& OutBytes ) const
{
    if( !NamesOtherLooseFile( InPath ) && ReadBytes( InPath.GetLocalPathString(), OutBytes ) )
    {
        return true;
    }
    if( InPath.FullPath.empty() )
    {
        return false;
    }
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
    if( !resolved.Exists || !resolved.Source || !resolved.Source->ReadText( resolved.RelativePath, OutText ) )
    {
        return false;
    }
    NoteRead( resolved );
    return true;
}


bool VFS::ReadBytes( const std::string& InLogicalPath, std::vector<uint8_t>& OutBytes ) const
{
    const VfsResolved resolved = Resolve( InLogicalPath );
    if( !resolved.Exists || !resolved.Source || !resolved.Source->ReadBytes( resolved.RelativePath, OutBytes ) )
    {
        return false;
    }
    NoteRead( resolved );
    return true;
}


bool VFS::WriteText( const std::string& InLogicalPath, const std::string& InText ) const
{
    SharedPtr<const MountTable> table = Snapshot();
    const std::string scheme = ServedScheme( *table, InLogicalPath );

    for( const VfsMount& mount : table->Mounts )
    {
        if( !Serves( mount, scheme, InLogicalPath ) || !mount.Writable )
        {
            continue;
        }
        return mount.Source->WriteText( RelativeTo( mount, InLogicalPath ), InText );
    }

    YIKES( "VFS: no writable mount serves " + InLogicalPath );
    return false;
}


void VFS::Enumerate( const std::string& InLogicalDirectory, bool InRecursive,
                     const std::function<void( const std::string& InLogicalPath, bool InIsDirectory )>& InVisitor ) const
{
    SharedPtr<const MountTable> table = Snapshot();
    const std::string scheme = ServedScheme( *table, InLogicalDirectory );

    // Higher-priority mounts fill the map first and win duplicates; the map keeps the visit sorted.
    std::map<std::string, bool> merged;
    for( const VfsMount& mount : table->Mounts )
    {
        if( !Serves( mount, scheme, InLogicalDirectory ) )
        {
            continue;
        }
        mount.Source->Enumerate( RelativeTo( mount, InLogicalDirectory ), InRecursive,
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


void VFS::SetRecordingReads( bool InRecording )
{
    m_recording.store( InRecording, std::memory_order_relaxed );
}


bool VFS::IsRecordingReads() const
{
    return m_recording.load( std::memory_order_relaxed );
}


std::vector<std::string> VFS::GetRecordedReads() const
{
    std::lock_guard<std::mutex> lock( m_recordMutex );
    return std::vector<std::string>( m_recorded.begin(), m_recorded.end() );
}


void VFS::ClearRecordedReads()
{
    std::lock_guard<std::mutex> lock( m_recordMutex );
    m_recorded.clear();
}


void VFS::NoteRead( const VfsResolved& InResolved ) const
{
    if( !m_recording.load( std::memory_order_relaxed ) )
    {
        return;
    }
    std::string spelling = RecordedSpelling( InResolved );
    if( spelling.empty() )
    {
        return;
    }
    std::lock_guard<std::mutex> lock( m_recordMutex );
    m_recorded.insert( std::move( spelling ) );
}


std::string VFS::RecordedSpelling( const VfsResolved& InResolved )
{
    if( !InResolved.Exists )
    {
        return {};
    }
    if( InResolved.MountPrefix == kEnginePrefix )
    {
        // An engine outside the project, read as "Engine/...": its assets are overlay content.
        const std::string assets = "Assets/";
        return InResolved.RelativePath.compare( 0, assets.size(), assets ) == 0 ? InResolved.RelativePath : kEnginePrefix + InResolved.RelativePath;
    }
    if( !InResolved.MountPrefix.empty() )
    {
        return {};
    }
    // Engine content read under its display spelling resolves on the game root as
    // "Engine/Assets/..."; the overlay (and so a cooked tree) serves it as "Assets/...".
    const std::string engineAssets = "Engine/Assets/";
    if( InResolved.RelativePath.compare( 0, engineAssets.size(), engineAssets ) == 0 )
    {
        return InResolved.RelativePath.substr( 7 );
    }
    return InResolved.RelativePath;
}
