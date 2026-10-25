#include <doctest/doctest.h>
#include "Path.h"
#include "File.h"
#include "VFS/VFS.h"
#include "VFS/Pak.h"
#include "Config/EngineConfig.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace VfsTest
{
    struct TempDir
    {
        explicit TempDir( const char* InRelative )
        {
            Absolute = std::filesystem::absolute( InRelative ).lexically_normal().generic_string();
            std::filesystem::remove_all( Absolute );
            std::filesystem::create_directories( Absolute );
        }

        ~TempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all( Absolute, ec );
        }

        std::string Absolute;
    };


    void WriteTextFile( const std::string& InPath, const std::string& InText )
    {
        std::filesystem::create_directories( std::filesystem::path( InPath ).parent_path() );
        std::ofstream stream( InPath );
        stream << InText;
    }


    struct ScopedTestMounts
    {
        ~ScopedTestMounts()
        {
            VFS::Get().UnmountAll( "vfstest://" );
        }
    };


    struct ScopedUserRoot
    {
        explicit ScopedUserRoot( const std::string& InRoot )
            : m_previous( VFS::Get().GetUserRoot() )
        {
            VFS::Get().SetUserRoot( InRoot );
        }

        ~ScopedUserRoot()
        {
            VFS::Get().SetUserRoot( m_previous );
        }

        std::string m_previous;
    };


    struct LegacyResolved
    {
        std::string FullPath;
        std::string LocalPath;
        bool Exists = false;
    };


    // The pre-VFS Path resolution, kept verbatim as the golden reference: game root over
    // "<cwd>/Engine/<trimmed>", with the Engine/Assets -> Assets rewrite for misses.
    LegacyResolved LegacyResolve( const std::string& InFile, bool Raw = false )
    {
        LegacyResolved result;
        std::string LocalPath;
        static const std::string s_programPath = []() {
            std::string path = std::filesystem::current_path().generic_string();
            std::replace( path.begin(), path.end(), '\\', '/' );
            return path;
        }();
        const std::string& ProgramPath = s_programPath;

        std::string assetPrefix = "/";
        LocalPath = InFile;

        std::replace( LocalPath.begin(), LocalPath.end(), '\\', '/' );

        if( LocalPath.empty() )
        {
            return result;
        }

        std::string::size_type slashPos = 0;
        while( ( slashPos = LocalPath.find( "//", slashPos ) ) != std::string::npos )
        {
            LocalPath.replace( slashPos, 2, "/" );
        }

#if USING( ME_PLATFORM_MACOS ) || USING( ME_PLATFORM_LINUX )
        size_t path = LocalPath[0] == '/' ? 0 : std::string::npos;
#else
        size_t path = LocalPath.find( ':' );
#endif
        if( path != std::string::npos )
        {
            result.FullPath = LocalPath;
        }
        else
        {
            result.FullPath = ProgramPath + assetPrefix + LocalPath;
        }

        path = LocalPath.rfind( "Assets" );
        if( path != std::string::npos )
        {
            LocalPath = LocalPath.substr( path, LocalPath.size() );
        }

        if( !std::filesystem::exists( result.FullPath ) )
        {
            if( !Raw )
            {
                std::string tempPath = ProgramPath + assetPrefix + "Engine/" + LocalPath;
                if( std::filesystem::exists( tempPath ) )
                {
                    result.FullPath = std::move( tempPath );
                    LocalPath = "Engine/" + LocalPath;
                    result.Exists = true;
                }
                else
                {
                    auto pos = result.FullPath.rfind( "Engine/Assets" );
                    if( pos != std::string::npos )
                    {
                        result.FullPath.replace( pos, 13, "Assets" );
                    }
                }
            }
        }
        else
        {
            result.Exists = true;
        }

        const size_t localPos = result.FullPath.rfind( LocalPath );
        result.LocalPath = result.FullPath.substr( localPos == std::string::npos ? 0 : localPos );
        return result;
    }
}

TEST_CASE( "VFS: overlay priority picks the higher mount and falls through" )
{
    VfsTest::ScopedTestMounts mounts;
    VfsTest::TempDir high( ".tmp/VfsTests/high" );
    VfsTest::TempDir low( ".tmp/VfsTests/low" );
    VfsTest::WriteTextFile( high.Absolute + "/both.txt", "high" );
    VfsTest::WriteTextFile( low.Absolute + "/both.txt", "low" );
    VfsTest::WriteTextFile( low.Absolute + "/only_low.txt", "low only" );

    VFS::Get().MountDirectory( "vfstest://", high.Absolute, 100 );
    VFS::Get().MountDirectory( "vfstest://", low.Absolute, 0 );

    Path both( "vfstest://both.txt" );
    CHECK( both.Exists );
    CHECK( both.IsFile );
    CHECK( both.FullPath == high.Absolute + "/both.txt" );

    std::string text;
    CHECK( VFS::Get().ReadText( std::string( "vfstest://both.txt" ), text ) );
    CHECK( text == "high" );

    Path onlyLow( "vfstest://only_low.txt" );
    CHECK( onlyLow.Exists );
    CHECK( onlyLow.FullPath == low.Absolute + "/only_low.txt" );

    // Misses land on the highest-priority mount: that's where a write would go.
    Path missing( "vfstest://missing.txt" );
    CHECK( !missing.Exists );
    CHECK( missing.FullPath == high.Absolute + "/missing.txt" );
}


TEST_CASE( "VFS: enumerate merges mounts and higher priority wins duplicates" )
{
    VfsTest::ScopedTestMounts mounts;
    VfsTest::TempDir high( ".tmp/VfsTests/high" );
    VfsTest::TempDir low( ".tmp/VfsTests/low" );
    VfsTest::WriteTextFile( high.Absolute + "/a.txt", "high" );
    VfsTest::WriteTextFile( high.Absolute + "/sub/b.txt", "high" );
    VfsTest::WriteTextFile( low.Absolute + "/a.txt", "low" );
    VfsTest::WriteTextFile( low.Absolute + "/c.txt", "low" );
    VfsTest::WriteTextFile( low.Absolute + "/sub/d.txt", "low" );

    VFS::Get().MountDirectory( "vfstest://", high.Absolute, 100 );
    VFS::Get().MountDirectory( "vfstest://", low.Absolute, 0 );

    std::map<std::string, bool> entries;
    VFS::Get().Enumerate( "vfstest://", true, [&]( const std::string& InLogicalPath, bool InIsDirectory )
    {
        entries[InLogicalPath] = InIsDirectory;
    } );

    REQUIRE( entries.size() == 5 );
    CHECK( entries.count( "vfstest://a.txt" ) == 1 );
    CHECK( entries.count( "vfstest://c.txt" ) == 1 );
    CHECK( entries.count( "vfstest://sub" ) == 1 );
    CHECK( entries["vfstest://sub"] );
    CHECK( entries.count( "vfstest://sub/b.txt" ) == 1 );
    CHECK( entries.count( "vfstest://sub/d.txt" ) == 1 );

    // The duplicate a.txt resolves through the higher mount.
    std::string text;
    CHECK( VFS::Get().ReadText( std::string( "vfstest://a.txt" ), text ) );
    CHECK( text == "high" );

    std::map<std::string, bool> shallow;
    VFS::Get().Enumerate( "vfstest://", false, [&]( const std::string& InLogicalPath, bool InIsDirectory )
    {
        shallow[InLogicalPath] = InIsDirectory;
    } );
    CHECK( shallow.size() == 3 );
    CHECK( shallow.count( "vfstest://sub/b.txt" ) == 0 );
}


TEST_CASE( "VFS: user:// writes create directories under the user root" )
{
    VfsTest::TempDir userDir( ".tmp/VfsTests/user" );
    VfsTest::ScopedUserRoot scopedRoot( userDir.Absolute );

    Path target( "user://Saves/Slot0/state.cfg" );
    CHECK( !target.Exists );
    CHECK( target.FullPath == userDir.Absolute + "/Saves/Slot0/state.cfg" );

    File file( target );
    file.Write( "hello user" );
    CHECK( std::filesystem::exists( userDir.Absolute + "/Saves/Slot0/state.cfg" ) );

    Path reread( "user://Saves/Slot0/state.cfg" );
    CHECK( reread.Exists );
    CHECK( reread.IsFile );
    File readFile( reread );
    CHECK( readFile.Read() == "hello user" );

    // The logical write API targets the writable user mount too.
    CHECK( VFS::Get().WriteText( std::string( "user://Notes/note.txt" ), "note" ) );
    std::string note;
    CHECK( VFS::Get().ReadText( std::string( "user://Notes/note.txt" ), note ) );
    CHECK( note == "note" );
}


TEST_CASE( "VFS: shipped builds keep Engine.cfg user state under user://" )
{
#if USING( ME_EDITOR )
    CHECK( std::string( EngineConfig::DefaultUserStatePath() ) == ".tmp/Engine.user.cfg" );
#else
    CHECK( std::string( EngineConfig::DefaultUserStatePath() ) == "user://Engine.cfg" );
    const std::string userRoot = VFS::Get().GetUserRoot();
    REQUIRE( !userRoot.empty() );
    Path cfg( EngineConfig::DefaultUserStatePath(), true );
    CHECK( cfg.FullPath == userRoot + "/Engine.cfg" );
#endif
}


TEST_CASE( "VFS: resolution matches the legacy resolver for every file in both Assets trees" )
{
    namespace fs = std::filesystem;
    REQUIRE( fs::exists( "Assets" ) );
    REQUIRE( fs::exists( "Engine/Assets" ) );

    int checked = 0;
    int mismatches = 0;
    auto compare = [&]( const std::string& InLogical )
    {
        const VfsTest::LegacyResolved legacy = VfsTest::LegacyResolve( InLogical );
        const Path vfsPath( InLogical );
        const bool same = vfsPath.FullPath == legacy.FullPath
            && vfsPath.GetLocalPathString() == legacy.LocalPath
            && vfsPath.Exists == legacy.Exists;
        if( !same && ++mismatches <= 10 )
        {
            CAPTURE( InLogical );
            CHECK( vfsPath.FullPath == legacy.FullPath );
            CHECK( vfsPath.GetLocalPathString() == legacy.LocalPath );
            CHECK( vfsPath.Exists == legacy.Exists );
        }
        ++checked;
    };

    for( fs::recursive_directory_iterator it( "Assets" ), end; it != end; ++it )
    {
        if( it->is_regular_file() )
        {
            compare( it->path().generic_string() );
        }
    }

    for( fs::recursive_directory_iterator it( "Engine/Assets" ), end; it != end; ++it )
    {
        if( !it->is_regular_file() )
        {
            continue;
        }
        const std::string engineLogical = it->path().generic_string();
        compare( engineLogical );
        // The spelling scenes actually use: the engine tree reached through the fallback.
        compare( engineLogical.substr( 7 ) );
    }

    CHECK( mismatches == 0 );
    CHECK( checked > 100 );
}


TEST_CASE( "vfs: mpak round-trips bytes, text, stats and enumeration" )
{
    using namespace VfsTest;
    TempDir dir( ".tmp/Tests/Pak" );
    const std::string pakPath = dir.Absolute + "/test.mpak";

    std::vector<uint8_t> binary;
    for( int i = 0; i < 300; ++i )
    {
        binary.push_back( static_cast<uint8_t>( i * 7 ) );
    }
    const std::string alpha = "alpha text\nwith lines";
    CHECK( Mpak::Write( pakPath, [&]( const Mpak::EntrySink& InSink ) {
        InSink( "Assets/a.txt", alpha.data(), alpha.size() );
        InSink( "Assets/Deep/b.bin", binary.data(), binary.size() );
        InSink( "Assets/empty.dat", nullptr, 0 );
    } ) );

    SharedPtr<PakMount> mount = PakMount::Open( pakPath );
    REQUIRE( mount != nullptr );
    CHECK( mount->GetEntryCount() == 3 );
    CHECK( mount->Stat( "Assets/a.txt" ).IsFile );
    CHECK( mount->Stat( "Assets/Deep" ).Exists );
    CHECK( !mount->Stat( "Assets/Deep" ).IsFile );
    CHECK( !mount->Stat( "Assets/missing" ).Exists );
    CHECK( mount->GetPhysicalPath( "Assets/a.txt" ).empty() );

    std::string text;
    CHECK( mount->ReadText( "Assets/a.txt", text ) );
    CHECK( text == alpha );
    std::vector<uint8_t> bytes;
    CHECK( mount->ReadBytes( "Assets/Deep/b.bin", bytes ) );
    CHECK( bytes == binary );
    CHECK( mount->ReadBytes( "Assets/empty.dat", bytes ) );
    CHECK( bytes.empty() );
    CHECK( !mount->WriteText( "Assets/a.txt", "read only" ) );

    std::vector<std::string> files;
    std::vector<std::string> directories;
    mount->Enumerate( "Assets", true, [&]( const std::string& InPath, bool InIsDirectory ) {
        ( InIsDirectory ? directories : files ).push_back( InPath );
    } );
    CHECK( files == std::vector<std::string>{ "Assets/Deep/b.bin", "Assets/a.txt", "Assets/empty.dat" } );
    CHECK( directories == std::vector<std::string>{ "Assets/Deep" } );

    VfsStreamWindow window;
    CHECK( mount->GetStreamWindow( "Assets/Deep/b.bin", window ) );
    CHECK( window.PhysicalFile == mount->GetPakPath() );
    CHECK( window.Size == binary.size() );
}


TEST_CASE( "vfs: a mounted pak serves logical paths, Path reads and windowed streams" )
{
    using namespace VfsTest;
    TempDir dir( ".tmp/Tests/PakMounted" );
    const std::string pakPath = dir.Absolute + "/world.mpak";

    std::vector<uint8_t> payload;
    for( int i = 0; i < 4096; ++i )
    {
        payload.push_back( static_cast<uint8_t>( ( i * 31 ) ^ ( i >> 3 ) ) );
    }
    const std::string note = "from the pak";
    REQUIRE( Mpak::Write( pakPath, [&]( const Mpak::EntrySink& InSink ) {
        InSink( "Assets/PakOnly/note.txt", note.data(), note.size() );
        InSink( "Assets/PakOnly/blob.bin", payload.data(), payload.size() );
    } ) );

    REQUIRE( VFS::Get().MountPak( "", pakPath, 300 ) );

    std::string text;
    CHECK( VFS::Get().ReadText( std::string( "Assets/PakOnly/note.txt" ), text ) );
    CHECK( text == note );

    // Path-based reads: no physical file exists, the logical identity answers.
    Path packed( "Assets/PakOnly/blob.bin" );
    CHECK( packed.Exists );
    std::vector<uint8_t> bytes;
    CHECK( VFS::Get().ReadBytes( packed, bytes ) );
    CHECK( bytes == payload );

    // The stream reads the pak window: entry-relative positions, clamped at the entry's end.
    VfsFileStream stream;
    REQUIRE( stream.Open( packed ) );
    CHECK( stream.Size() == payload.size() );
    std::vector<uint8_t> chunk( 100 );
    CHECK( stream.Read( chunk.data(), chunk.size() ) == 100 );
    CHECK( std::equal( chunk.begin(), chunk.end(), payload.begin() ) );
    REQUIRE( stream.Seek( 4000 ) );
    CHECK( stream.Read( chunk.data(), chunk.size() ) == 96 );
    CHECK( std::equal( chunk.begin(), chunk.begin() + 96, payload.begin() + 4000 ) );
    CHECK( stream.Read( chunk.data(), chunk.size() ) == 0 );
    REQUIRE( stream.Seek( 0 ) );
    CHECK( stream.Read( chunk.data(), 1 ) == 1 );
    CHECK( chunk[0] == payload[0] );
    stream.Close();

    VFS::Get().ResetToDefaults();
}


TEST_CASE( "vfs: read recording notes served overlay paths in their overlay spelling" )
{
    using namespace VfsTest;
    TempDir dir( ".tmp/Tests/VfsRecording" );
    WriteTextFile( dir.Absolute + "/Engine/Assets/RecordTest/engine.txt", "engine" );
    WriteTextFile( dir.Absolute + "/Assets/RecordTest/game.txt", "game" );
    WriteTextFile( dir.Absolute + "/Assets/RecordTest/streamed.bin", "0123456789" );
    WriteTextFile( dir.Absolute + "/Assets/RecordTest/untouched.txt", "never read" );
    TempDir userDir( ".tmp/Tests/VfsRecordingUser" );
    ScopedUserRoot user( userDir.Absolute );
    WriteTextFile( userDir.Absolute + "/settings.json", "{}" );
    VFS::Get().MountDirectory( "", dir.Absolute, 300 );

    VFS::Get().ClearRecordedReads();
    std::string text;
    // Off: nothing is noted.
    CHECK( VFS::Get().ReadText( std::string( "Assets/RecordTest/game.txt" ), text ) );
    CHECK( VFS::Get().GetRecordedReads().empty() );

    VFS::Get().SetRecordingReads( true );
    CHECK( VFS::Get().ReadText( std::string( "Assets/RecordTest/game.txt" ), text ) );
    std::vector<uint8_t> bytes;
    CHECK( VFS::Get().ReadBytes( std::string( "Engine/Assets/RecordTest/engine.txt" ), bytes ) );
    VfsFileStream stream;
    REQUIRE( stream.Open( Path( "Assets/RecordTest/streamed.bin" ) ) );
    stream.Close();
    // Misses, existence probes and user state aren't dependencies.
    CHECK( !VFS::Get().ReadText( std::string( "Assets/RecordTest/missing.txt" ), text ) );
    CHECK( Path( "Assets/RecordTest/untouched.txt" ).Exists );
    CHECK( VFS::Get().ReadText( std::string( "user://settings.json" ), text ) );
    VFS::Get().SetRecordingReads( false );

    const std::vector<std::string> expected = { "Assets/RecordTest/engine.txt", "Assets/RecordTest/game.txt", "Assets/RecordTest/streamed.bin" };
    std::vector<std::string> recorded;
    for( const std::string& read : VFS::Get().GetRecordedReads() )
    {
        if( read.find( "RecordTest" ) != std::string::npos ) recorded.push_back( read );
    }
    CHECK( recorded == expected );

    VFS::Get().ClearRecordedReads();
    VFS::Get().ResetToDefaults();
}
