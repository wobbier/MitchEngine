#include "PCH.h"
#include "Tools/Build/BuildGame.h"

#include "CLog.h"
#include "Dementia.h"
#include "Engine/Project.h"
#include "JSON.h"
#include "VFS/VFS.h"
#include "Resource/CookedAssets.h"
#include "Tools/Cook/Cooker.h"
#include "Utils/PlatformUtils.h"
#include "SDL.h"
#if USING( ME_SCRIPTING )
#include "Scripting/ScriptEngine.h"
#include "Scripting/ScriptHost.h"
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>

namespace BuildGame
{
    namespace fs = std::filesystem;

    namespace
    {
        // A name usable as a file name everywhere: no path separators, reserved characters or
        // control characters, no leading/trailing spaces or dots.
        std::string FileSafe( const std::string& InName, const char* InFallback )
        {
            constexpr std::string_view kReserved = "/\\:*?\"<>|";
            std::string name;
            for( char c : InName )
            {
                if( static_cast<unsigned char>( c ) >= 32 && kReserved.find( c ) == std::string_view::npos )
                {
                    name += c;
                }
            }
            const size_t first = name.find_first_not_of( " ." );
            const size_t last = name.find_last_not_of( " ." );
            name = first == std::string::npos ? std::string() : name.substr( first, last - first + 1 );
            return name.empty() ? std::string( InFallback ) : name;
        }
    }


    const char* StepName( Step InStep )
    {
        switch( InStep )
        {
        case Step::Compile:
            return "Compile";
        case Step::Scripts:
            return "Scripts";
        case Step::Cook:
            return "Cook";
        case Step::Runtime:
            return "Runtime";
        case Step::Package:
            return "Package";
        default:
            return "";
        }
    }


    std::string OutputDirectory( const ProjectSettings::BuildProfile& InProfile )
    {
        return InProfile.OutputDirectory.empty() ? "Builds/" + FileSafe( InProfile.Name, "Default" ) : InProfile.OutputDirectory;
    }


    namespace
    {
        // An executable in a configuration's game build folder, e.g. ".build/Game_linux_Retail/Player".
        std::string BuildExecutable( const std::string& InConfiguration, const std::string& InStem )
        {
#if USING( ME_PLATFORM_WINDOWS )
            return ".build/Game_win64_" + InConfiguration + "/" + InStem + ".exe";
#elif USING( ME_PLATFORM_MACOS )
            return ".build/Game_macos_" + InConfiguration + "/" + InStem;
#else
            return ".build/Game_linux_" + InConfiguration + "/" + InStem;
#endif
        }


        // This editor build's directory (".../.build/Editor_Debug").
        fs::path EditorBuildDirectory()
        {
            std::string base;
            if( char* path = SDL_GetBasePath() )
            {
                base = path;
                SDL_free( path );
            }
            std::error_code error;
            return fs::absolute( base.empty() ? fs::path( "." ) : fs::path( base ), error ).lexically_normal().parent_path();
        }


        // InExecutable (a BuildExecutable path) in the game build folders beside this editor build.
        fs::path BesideEditor( const std::string& InExecutable )
        {
            return EditorBuildDirectory().parent_path() / fs::path( InExecutable ).lexically_relative( ".build" );
        }
    }


    std::string GameExecutable( const std::string& InConfiguration )
    {
        return BuildExecutable( InConfiguration, "Game_EntryPoint_x64" );
    }


    std::string PlayerExecutable( const std::string& InConfiguration )
    {
        return BuildExecutable( InConfiguration, "Player" );
    }


    bool ShipsPlayer()
    {
        return !Project::HasGameModule();
    }


    std::string RuntimeExecutable( const std::string& InConfiguration )
    {
        std::error_code error;
        const fs::path executable = ShipsPlayer() ? BesideEditor( PlayerExecutable( InConfiguration ) ) : fs::absolute( GameExecutable( InConfiguration ), error );
        return executable.lexically_normal().generic_string();
    }


    std::string RecordingConfiguration()
    {
        return EditorBuildDirectory().filename().string().find( "Release" ) != std::string::npos ? "Release" : "Debug";
    }


    std::string FindRecordingExecutable()
    {
        // The newest, not the first: a configuration nobody built for a while may predate engine
        // changes and no longer start.
        std::error_code error;
        fs::path newest;
        fs::file_time_type newestTime = fs::file_time_type::min();
        for( const char* configuration : { "Debug", "Release" } )
        {
            const fs::path game = BesideEditor( ShipsPlayer() ? PlayerExecutable( configuration ) : GameExecutable( configuration ) );
            if( fs::is_regular_file( game, error ) && fs::last_write_time( game, error ) > newestTime )
            {
                newest = game;
                newestTime = fs::last_write_time( game, error );
            }
        }
        return newest.generic_string();
    }


    std::string ShippedExecutableName( const std::string& InProductName )
    {
        const std::string name = FileSafe( InProductName, "Game" );
#if USING( ME_PLATFORM_WINDOWS )
        return name + ".exe";
#elif USING( ME_PLATFORM_LINUX )
        return name + ".x86_64";
#else
        return name;
#endif
    }


#if USING( ME_TOOLS )
    namespace
    {
        bool IsKnownConfiguration( const std::string& InConfiguration )
        {
            return InConfiguration == "Retail" || InConfiguration == "Release" || InConfiguration == "Debug";
        }


        std::string Lower( std::string InText )
        {
            std::transform( InText.begin(), InText.end(), InText.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
            return InText;
        }


        // "8.0.31" -> { 8, 0, 31 }: version directories compare numerically, not as text.
        std::vector<int> ParseVersion( const std::string& InVersion )
        {
            std::vector<int> parts;
            size_t start = 0;
            while( start <= InVersion.size() )
            {
                const size_t dot = InVersion.find( '.', start );
                const std::string part = InVersion.substr( start, dot == std::string::npos ? std::string::npos : dot - start );
                parts.push_back( std::atoi( part.c_str() ) );
                if( dot == std::string::npos )
                {
                    break;
                }
                start = dot + 1;
            }
            return parts;
        }


        // The newest version directory under InParent whose name starts with InPrefix ("" = any).
        std::string NewestVersion( const fs::path& InParent, const std::string& InPrefix )
        {
            std::string best;
            std::error_code error;
            for( fs::directory_iterator it( InParent, error ), end; !error && it != end; it.increment( error ) )
            {
                const std::string name = it->path().filename().string();
                if( it->is_directory( error ) && name.rfind( InPrefix, 0 ) == 0 && ( best.empty() || ParseVersion( name ) > ParseVersion( best ) ) )
                {
                    best = name;
                }
            }
            return best;
        }


        uint64_t TreeSize( const fs::path& InRoot )
        {
            uint64_t bytes = 0;
            std::error_code error;
            for( fs::recursive_directory_iterator it( InRoot, error ), end; !error && it != end; it.increment( error ) )
            {
                if( it->is_regular_file( error ) )
                {
                    bytes += it->file_size( error );
                }
            }
            return bytes;
        }


        std::string MiB( uint64_t InBytes )
        {
            return std::to_string( ( InBytes + 512 * 1024 ) / ( 1024 * 1024 ) ) + " MiB";
        }


        class Builder
        {
        public:
            Builder( const Request& InRequest, Report& OutReport )
                : m_request( InRequest )
                , m_profile( InRequest.Profile )
                , m_report( OutReport )
            {
            }


            void Run()
            {
                if( !Prepare() )
                {
                    return;
                }
                for( int index = 0; index < static_cast<int>( Step::Count ); ++index )
                {
                    const Step step = static_cast<Step>( index );
                    if( m_request.Cancel && m_request.Cancel->load() )
                    {
                        Error( "Cancelled" );
                        return;
                    }
                    if( m_request.OnStep )
                    {
                        m_request.OnStep( step );
                    }
                    Info( std::string( "== " ) + StepName( step ) );
                    if( !RunStep( step ) )
                    {
                        return;
                    }
                }
                m_report.Success = m_report.Errors.empty();
            }


        private:
            bool RunStep( Step InStep )
            {
                switch( InStep )
                {
                case Step::Compile:
                    return Compile();
                case Step::Scripts:
                    return Scripts();
                case Step::Cook:
                    return CookContent();
                case Step::Runtime:
                    return Runtime();
                case Step::Package:
                    return Package();
                default:
                    return true;
                }
            }


            // Build.log gets everything; the log gets the builder's own messages, and OnLine the
            // commands' output.
            void LogFile( const std::string& InLine )
            {
                if( m_log.is_open() )
                {
                    m_log << InLine << '\n';
                    m_log.flush();
                }
            }


            void ToolLine( const std::string& InLine )
            {
                LogFile( InLine );
                if( m_request.OnLine )
                {
                    m_request.OnLine( InLine );
                }
            }


            void Info( const std::string& InMessage )
            {
                CLog::Log( CLog::LogType::Info, "Build Game: " + InMessage );
                LogFile( InMessage );
            }


            void Warn( const std::string& InMessage )
            {
                CLog::Log( CLog::LogType::Warning, "Build Game: " + InMessage );
                m_report.Warnings.push_back( InMessage );
                LogFile( "warning: " + InMessage );
            }


            void Error( const std::string& InMessage )
            {
                CLog::Log( CLog::LogType::Error, "Build Game: " + InMessage );
                m_report.Errors.push_back( InMessage );
                LogFile( "error: " + InMessage );
            }


            // Runs a shell command, streaming its output into the build log. Lines that mention an
            // error are kept, so a failure can say why without the whole log.
            int Command( const std::string& InCommand )
            {
                ToolLine( "> " + InCommand );
                m_errorLines.clear();
                return PlatformUtils::RunCommand( InCommand, [this]( const std::string& InLine ) {
                    ToolLine( InLine );
                    if( m_errorLines.size() < 20 && Lower( InLine ).find( "error" ) != std::string::npos )
                    {
                        m_errorLines.push_back( InLine );
                    }
                } );
            }


            void CommandFailed( const std::string& InWhat, int InStatus )
            {
                for( const std::string& line : m_errorLines )
                {
                    m_report.Errors.push_back( line );
                }
                Error( InWhat + " failed (exit " + std::to_string( InStatus ) + "); the full output is in " + m_logPath.generic_string() );
            }


            // Copies a directory's files (following symlinks) and makes the copies writable, so the
            // next build can clear them: installs like the Nix store's are read-only.
            bool CopyTree( const fs::path& InFrom, const fs::path& InTo )
            {
                std::error_code error;
                for( fs::recursive_directory_iterator it( InFrom, fs::directory_options::follow_directory_symlink, error ), end; !error && it != end; it.increment( error ) )
                {
                    const fs::path target = InTo / it->path().lexically_relative( InFrom );
                    if( it->is_directory( error ) )
                    {
                        fs::create_directories( target, error );
                        continue;
                    }
                    if( !CopyFile( it->path(), target ) )
                    {
                        return false;
                    }
                }
                if( error )
                {
                    Error( "Couldn't copy " + InFrom.generic_string() + " (" + error.message() + ")" );
                    return false;
                }
                return true;
            }


            bool CopyFile( const fs::path& InFrom, const fs::path& InTo )
            {
                std::error_code error;
                fs::create_directories( InTo.parent_path(), error );
                fs::copy_file( InFrom, InTo, fs::copy_options::overwrite_existing, error );
                if( !error )
                {
                    fs::permissions( InTo, fs::perms::owner_write, fs::perm_options::add, error );
                }
                if( error )
                {
                    Error( "Couldn't copy " + InFrom.generic_string() + " to " + InTo.generic_string() + " (" + error.message() + ")" );
                    return false;
                }
                return true;
            }


            bool Prepare()
            {
                if( !IsKnownConfiguration( m_profile.Configuration ) )
                {
                    Error( "Unknown configuration '" + m_profile.Configuration + "' (Retail, Release or Debug)" );
                    return false;
                }
                std::error_code error;
                m_out = fs::absolute( OutputDirectory( m_profile ), error ).lexically_normal();
                if( !m_out.has_filename() )
                {
                    m_out = m_out.parent_path();
                }
                m_report.OutputDirectory = m_out.generic_string();
                m_staging = fs::absolute( ".tmp/BuildGame/Staging/" + FileSafe( m_profile.Name, "Default" ), error ).lexically_normal();
                const std::string staging = m_staging.generic_string() + "/";
                if( staging.rfind( m_out.generic_string() + "/", 0 ) == 0 )
                {
                    Error( "The output folder " + m_out.generic_string() + " can't hold the build's staging folder " + m_staging.generic_string() );
                    return false;
                }
                // Only ever replace an earlier build (or an empty directory).
                if( fs::exists( m_out, error ) )
                {
                    if( !fs::is_empty( m_out, error ) && !fs::exists( m_out / "BuildInfo.json", error ) )
                    {
                        Error( "Won't replace " + m_out.generic_string() + ": it isn't an earlier build (no BuildInfo.json); pick an empty folder" );
                        return false;
                    }
                    fs::remove_all( m_out, error );
                    if( error )
                    {
                        Error( "Couldn't clear " + m_out.generic_string() + " (" + error.message() + ")" );
                        return false;
                    }
                }
                fs::create_directories( m_out, error );
                fs::create_directories( m_staging, error );
                m_logPath = m_staging / "Build.log";
                m_log.open( m_logPath, std::ios::out | std::ios::trunc );
                // Marks the folder as a build output from the start, so a failed build can be
                // replaced too.
                WriteBuildInfo();
                Info( "Building '" + m_profile.Name + "' (" + m_profile.Configuration + ", " + Cook::PlatformName( Cook::HostPlatform() ) + ") into " + m_out.generic_string() );
                return true;
            }


            // The solution makefile Sharpmake generates in a checkout's root (<Solution>.make)
            // that has InTarget.
            static std::string FindSolutionMakefile( const fs::path& InDirectory, const std::string& InTarget )
            {
                std::error_code error;
                for( fs::directory_iterator it( InDirectory, error ), end; !error && it != end; it.increment( error ) )
                {
                    if( it->path().extension() != ".make" )
                    {
                        continue;
                    }
                    std::ifstream file( it->path() );
                    for( std::string line; std::getline( file, line ); )
                    {
                        if( line.rfind( InTarget + ":", 0 ) == 0 )
                        {
                            return it->path().filename().string();
                        }
                    }
                }
                return std::string();
            }


            // Builds a configuration's game executable through the solution makefile: the
            // project's Game_EntryPoint, or for a C#-only project (no solution of its own) the
            // Player, in the checkout this editor was built in.
            bool MakeGame( const std::string& InConfiguration )
            {
#if USING( ME_PLATFORM_LINUX )
                const bool player = ShipsPlayer();
                const std::string target = player ? "Player" : "Game_EntryPoint";
                std::error_code error;
                const fs::path checkout = player ? EditorBuildDirectory().parent_path().parent_path() : fs::current_path( error );
                const std::string makefile = FindSolutionMakefile( checkout, target );
                if( makefile.empty() )
                {
                    Error( player ? "No solution makefile with a Player target in " + checkout.generic_string()
                            + ", the checkout this editor was built in; build the Player there yourself, then Build Game without compiling (--no-compile)"
                                  : "No solution makefile with a Game_EntryPoint target in the project root; generate the project first (Project/GenerateSolution.sh)" );
                    return false;
                }
                const unsigned jobs = std::max( 1u, std::thread::hardware_concurrency() );
                const int status = Command( "make -C \"" + checkout.generic_string() + "\" -f \"" + makefile + "\" " + target + " config=game_linux_" + Lower( InConfiguration )
                    + " -j" + std::to_string( jobs ) );
                if( status != 0 )
                {
                    CommandFailed( "Compiling the " + InConfiguration + ( player ? " Player" : " game" ), status );
                    return false;
                }
                return true;
#else
                Error( "Build Game can't compile on this platform yet: build the game's " + InConfiguration + " configuration in your IDE, then build without compiling (--no-compile)" );
                return false;
#endif
            }


            bool Compile()
            {
                m_executable = RuntimeExecutable( m_profile.Configuration );
                // The cooker records through a tools game build. Unless one was named, it's the one
                // matching this editor, compiled here too, so a stale build never records.
                const std::string recordConfiguration = RecordingConfiguration();
                const bool compileRecorder = m_request.Record && m_request.RecordExecutable.empty() && recordConfiguration != m_profile.Configuration;
                if( m_request.Compile && ( !MakeGame( m_profile.Configuration ) || ( compileRecorder && !MakeGame( recordConfiguration ) ) ) )
                {
                    return false;
                }
                std::error_code error;
                if( !fs::is_regular_file( m_executable, error ) )
                {
                    Error( "No " + m_profile.Configuration + ( ShipsPlayer() ? " Player at " : " game executable at " ) + m_executable.generic_string() );
                    return false;
                }
                Info( std::string( ShipsPlayer() ? "Player (a C#-only project): " : "Game executable: " ) + m_executable.generic_string() );
                // Its libraries come from the tree it was built in (<tree>/.build/<config>/<exe>).
                m_buildTree = m_executable.parent_path().parent_path().parent_path();
                if( m_request.Record )
                {
                    m_recordExecutable = !m_request.RecordExecutable.empty() ? m_request.RecordExecutable
                        : m_request.Compile ? RuntimeExecutable( recordConfiguration )
                        : FindRecordingExecutable();
                    if( m_recordExecutable.empty() || !fs::is_regular_file( m_recordExecutable, error ) )
                    {
                        Warn( "No tools game build to record the scenes with" + ( m_recordExecutable.empty() ? std::string() : " at " + m_recordExecutable )
                            + ": the cook is static, and assets only scripts load by name won't ship" );
                        m_recordExecutable.clear();
                    }
                    else
                    {
                        Info( "Recording with: " + m_recordExecutable );
                    }
                }
                return true;
            }


            bool Scripts()
            {
#if USING( ME_SCRIPTING )
                std::error_code error;
                if( !fs::exists( ScriptEngine::kScriptProject, error ) )
                {
                    Warn( std::string( "No script project (" ) + ScriptEngine::kScriptProject + "): the build has no C# scripts" );
                    return true;
                }
                const fs::path scripts = m_staging / "Scripts";
                fs::remove_all( scripts, error );
                // Retail ships optimized scripts; Debug keeps them debuggable. MitchEngineDir points
                // the script project at this engine's ScriptCore, as ScriptEngine::BuildScripts
                // does: a C#-only project has no Engine/ of its own.
                const std::string configuration = m_profile.Configuration == "Debug" ? "Debug" : "Release";
                const int status = Command( std::string( "dotnet build \"" ) + ScriptEngine::kScriptProject + "\" -c " + configuration + " -o \"" + scripts.generic_string()
                    + "\" -p:MitchEngineDir=\"" + Project::GetEngineRoot() + "/\" --artifacts-path \"" + ( m_staging / "dotnet" ).generic_string() + "\" --nologo -v q -clp:NoSummary" );
                if( status != 0 )
                {
                    CommandFailed( "Building the scripts", status );
                    return false;
                }
                int count = 0;
                for( fs::recursive_directory_iterator it( scripts, error ), end; !error && it != end; it.increment( error ) )
                {
                    const std::string name = it->path().filename().string();
                    const std::string extension = it->path().extension().string();
                    // The source generators run at compile time; symbols stay out of Retail.
                    if( !it->is_regular_file( error ) || name.rfind( "MitchEngine.Generators", 0 ) == 0 || ( extension == ".pdb" && m_profile.Configuration == "Retail" ) )
                    {
                        continue;
                    }
                    if( !CopyFile( it->path(), m_out / it->path().lexically_relative( scripts ) ) )
                    {
                        return false;
                    }
                    count += extension == ".dll" ? 1 : 0;
                }
                m_hasScripts = true;
                Info( "Scripts: " + std::to_string( count ) + " assemblies (" + configuration + ")" );
#else
                Warn( "This editor build has no .NET scripting: the build has no C# scripts" );
#endif
                return true;
            }


            bool CookContent()
            {
                Cook::Request cook;
                cook.Scenes = m_profile.Scenes;
                cook.OutputDirectory = ( m_staging / "Cook" ).generic_string();
                if( m_profile.Pak )
                {
                    cook.PakFile = ( m_out / CookedAssets::kShippedPak ).generic_string();
                }
                cook.GameExecutable = m_recordExecutable;
                if( !m_request.Record )
                {
                    Info( "Static cook (recording is off): assets only scripts load by name won't ship" );
                }
                const Cook::Report report = Cook::Run( cook );
                for( const std::string& warning : report.Warnings )
                {
                    Warn( "Cook: " + warning );
                }
                if( !report.Success )
                {
                    for( const std::string& error : report.Errors )
                    {
                        Error( "Cook: " + error );
                    }
                    if( report.Errors.empty() )
                    {
                        Error( "The cook failed" );
                    }
                    return false;
                }
                Info( "Cook: " + std::to_string( report.FileCount ) + " files, " + MiB( report.ByteCount ) + ( m_profile.Pak ? std::string( " in " ) + CookedAssets::kShippedPak : std::string( " (loose)" ) ) );
                return m_profile.Pak || CopyTree( m_staging / "Cook", m_out );
            }


            bool Runtime()
            {
#if USING( ME_SCRIPTING )
                if( !m_hasScripts )
                {
                    Info( "No scripts, so no .NET runtime" );
                    return true;
                }
                const fs::path root = ScriptHost::FindSystemDotnetRoot();
                if( root.empty() )
                {
                    Error( "No .NET install to ship the runtime from (install .NET 8 or set DOTNET_ROOT)" );
                    return false;
                }
                // The framework the scripts target (ScriptCore.runtimeconfig.json), newest patch.
                std::string framework = "Microsoft.NETCore.App";
                std::string version = "8.0.0";
                std::ifstream config( m_out / "ScriptCore.runtimeconfig.json" );
                const json runtimeConfig = json::parse( config, nullptr, false );
                if( !runtimeConfig.is_discarded() && runtimeConfig.contains( "runtimeOptions" ) )
                {
                    const json& options = runtimeConfig["runtimeOptions"];
                    if( options.contains( "framework" ) && options["framework"].is_object() )
                    {
                        framework = options["framework"].value( "name", framework );
                        version = options["framework"].value( "version", version );
                    }
                }
                const std::string major = version.substr( 0, version.find( '.' ) ) + ".";
                const std::string runtimeVersion = NewestVersion( root / "shared" / framework, major );
                const std::string fxrVersion = NewestVersion( root / "host" / "fxr", "" );
                if( runtimeVersion.empty() || fxrVersion.empty() )
                {
                    Error( "The .NET install at " + root.generic_string() + " has no " + framework + " " + major + "x runtime" );
                    return false;
                }
                const fs::path dotnet = m_out / "dotnet";
                if( !CopyTree( root / "host" / "fxr" / fxrVersion, dotnet / "host" / "fxr" / fxrVersion )
                    || !CopyTree( root / "shared" / framework / runtimeVersion, dotnet / "shared" / framework / runtimeVersion ) )
                {
                    return false;
                }
                // .NET is MIT licensed; its notices travel with it.
                for( const char* notice : { "LICENSE.txt", "ThirdPartyNotices.txt" } )
                {
                    std::error_code error;
                    if( fs::is_regular_file( root / notice, error ) && !CopyFile( root / notice, dotnet / notice ) )
                    {
                        return false;
                    }
                }
                m_dotnetVersion = runtimeVersion;
                Info( ".NET " + runtimeVersion + " (app-local, " + MiB( TreeSize( dotnet ) ) + ") from " + root.generic_string() );
#else
                Info( "No scripting, so no .NET runtime" );
#endif
                return true;
            }


#if USING( ME_PLATFORM_LINUX )
            // The shared libraries a distro doesn't provide (or provides in other versions) go
            // into lib/: SDL2, the C++ runtime, nethost and whatever links from the tree the
            // executable was built in (FMOD). Left to the system: glibc, the GL/Vulkan drivers, X11/Wayland, ALSA, curl.
            bool BundleLibraries( const fs::path& InExecutable )
            {
                std::string output;
                if( PlatformUtils::RunCommand( "ldd \"" + InExecutable.generic_string() + "\"", output ) != 0 )
                {
                    Error( "ldd couldn't read " + InExecutable.generic_string() + ":\n" + output );
                    return false;
                }
                const std::string tree = m_buildTree.generic_string() + "/";
                const fs::path lib = m_out / "lib";
                std::vector<std::string> bundled;
                size_t start = 0;
                while( start < output.size() )
                {
                    size_t end = output.find( '\n', start );
                    end = end == std::string::npos ? output.size() : end;
                    const std::string line = output.substr( start, end - start );
                    start = end + 1;
                    // "\tlibSDL2-2.0.so.0 => /nix/store/.../libSDL2-2.0.so.0 (0x...)"
                    const size_t arrow = line.find( " => /" );
                    if( arrow == std::string::npos )
                    {
                        continue;
                    }
                    const size_t nameStart = line.find_first_not_of( " \t" );
                    const std::string name = line.substr( nameStart, arrow - nameStart );
                    const size_t pathEnd = line.find( " (", arrow );
                    const std::string path = line.substr( arrow + 4, pathEnd == std::string::npos ? std::string::npos : pathEnd - arrow - 4 );
                    const bool runtime = name.rfind( "libSDL2", 0 ) == 0 || name.rfind( "libstdc++", 0 ) == 0 || name.rfind( "libgcc_s", 0 ) == 0 || name.rfind( "libnethost", 0 ) == 0;
                    if( runtime || path.rfind( tree, 0 ) == 0 )
                    {
                        if( !CopyFile( path, lib / name ) )
                        {
                            return false;
                        }
                        bundled.push_back( name );
                    }
                }
                std::string names;
                for( const std::string& name : bundled )
                {
                    names += ( names.empty() ? "" : ", " ) + name;
                }
                Info( "Libraries: " + names );
                return true;
            }


            bool WriteLauncher( const fs::path& InLauncher, const std::string& InExecutable )
            {
                const ProjectSettings& settings = ProjectSettings::Get();
                std::ofstream launcher( InLauncher, std::ios::out | std::ios::trunc );
                launcher << "#!/bin/sh\n"
                         << "# " << settings.ProductName << " " << settings.ProductVersion << " (made with Build Game): runs the game with the libraries\n"
                         << "# in lib/ and the .NET runtime in dotnet/, from any working directory.\n"
                         << "cd \"$(dirname \"$0\")\" || exit 1\n"
                         << "LD_LIBRARY_PATH=\"$PWD/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\" exec \"./" << InExecutable << "\" \"$@\"\n";
                launcher.close();
                std::error_code error;
                fs::permissions( InLauncher, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace, error );
                if( !launcher || error )
                {
                    Error( "Couldn't write the launcher " + InLauncher.generic_string() );
                    return false;
                }
                return true;
            }
#endif


            bool Package()
            {
                const ProjectSettings& settings = ProjectSettings::Get();
                const std::string executable = ShippedExecutableName( settings.ProductName );
                if( !CopyFile( m_executable, m_out / executable ) )
                {
                    return false;
                }
#if USING( ME_PLATFORM_LINUX )
                const fs::path launcher = m_out / ( FileSafe( settings.ProductName, "Game" ) + ".sh" );
                if( !BundleLibraries( m_out / executable ) || !WriteLauncher( launcher, executable ) )
                {
                    return false;
                }
                m_launcher = launcher.filename().string();
#elif USING( ME_PLATFORM_WINDOWS )
                // Windows finds DLLs next to the executable: the ones its build copied there
                // (SDL2, FMOD, nethost) ship with it.
                std::error_code error;
                for( fs::directory_iterator it( m_executable.parent_path(), error ), end; !error && it != end; it.increment( error ) )
                {
                    const fs::path target = m_out / it->path().filename();
                    if( it->path().extension() == ".dll" && !fs::exists( target, error ) && !CopyFile( it->path(), target ) )
                    {
                        return false;
                    }
                }
                m_launcher = executable;
#else
                m_launcher = executable;
#endif
                m_report.Launcher = ( m_out / m_launcher ).generic_string();

                // The icon beside the build too, for desktop entries and store pages (the window
                // icon itself comes from the content).
                if( !settings.ProductIcon.empty() )
                {
                    std::vector<uint8_t> icon;
                    const std::string extension = fs::path( settings.ProductIcon ).extension().string();
                    if( VFS::Get().ReadBytes( settings.ProductIcon, icon ) )
                    {
                        std::ofstream file( m_out / ( "icon" + extension ), std::ios::out | std::ios::binary | std::ios::trunc );
                        file.write( reinterpret_cast<const char*>( icon.data() ), static_cast<std::streamsize>( icon.size() ) );
                    }
                    else
                    {
                        Warn( "Couldn't read the product icon " + settings.ProductIcon );
                    }
                }

                if( !WriteBuildInfo() )
                {
                    return false;
                }
                if( m_profile.Archive && !Archive() )
                {
                    return false;
                }
                return true;
            }


            bool Archive()
            {
#if USING( ME_PLATFORM_WINDOWS )
                const std::string extension = ".zip";
                const std::string flags = "-a -cf";
#else
                const std::string extension = ".tar.gz";
                const std::string flags = "-czf";
#endif
                const fs::path archive = m_out.parent_path() / ( m_out.filename().string() + extension );
                std::error_code error;
                fs::remove( archive, error );
                const int status = Command( "tar " + flags + " \"" + archive.generic_string() + "\" -C \"" + m_out.parent_path().generic_string() + "\" \"" + m_out.filename().string() + "\"" );
                if( status != 0 )
                {
                    CommandFailed( "Archiving the build", status );
                    return false;
                }
                Info( "Archive: " + archive.generic_string() + " (" + MiB( fs::file_size( archive, error ) ) + ")" );
                return true;
            }


            bool WriteBuildInfo()
            {
                const ProjectSettings& settings = ProjectSettings::Get();
                const std::time_t now = std::time( nullptr );
                char built[32] = {};
                std::strftime( built, sizeof( built ), "%Y-%m-%dT%H:%M:%SZ", std::gmtime( &now ) );
                json info;
                info["Product"] = { { "Name", settings.ProductName }, { "Org", settings.ProductOrg }, { "Version", settings.ProductVersion } };
                info["Profile"] = m_profile.Name;
                info["Configuration"] = m_profile.Configuration;
                info["Platform"] = Cook::PlatformName( Cook::HostPlatform() );
                info["Launcher"] = m_launcher;
                info["Content"] = m_profile.Pak ? std::string( CookedAssets::kShippedPak ) : std::string( "loose" );
                info["DotNet"] = m_dotnetVersion;
                info["Built"] = built;
                std::ofstream file( m_out / "BuildInfo.json", std::ios::out | std::ios::trunc );
                file << info.dump( 4 ) << '\n';
                if( !file )
                {
                    Error( "Couldn't write " + ( m_out / "BuildInfo.json" ).generic_string() );
                    return false;
                }
                return true;
            }

            const Request& m_request;
            const ProjectSettings::BuildProfile& m_profile;
            Report& m_report;
            fs::path m_out;
            fs::path m_staging;
            fs::path m_logPath;
            fs::path m_executable;
            fs::path m_buildTree;
            std::ofstream m_log;
            std::vector<std::string> m_errorLines;
            std::string m_recordExecutable;
            std::string m_launcher;
            std::string m_dotnetVersion;
            bool m_hasScripts = false;
        };
    }
#endif


    Report Run( const Request& InRequest )
    {
        Report report;
#if USING( ME_TOOLS )
        const auto start = std::chrono::steady_clock::now();
        Builder builder( InRequest, report );
        builder.Run();
        report.Seconds = std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
        std::error_code error;
        if( !report.OutputDirectory.empty() && fs::exists( report.OutputDirectory, error ) )
        {
            report.ByteCount = TreeSize( report.OutputDirectory );
        }
        const std::string summary = report.Success
            ? "Build Game finished: " + report.OutputDirectory + " (" + MiB( report.ByteCount ) + ", " + std::to_string( static_cast<int>( report.Seconds ) ) + " s, "
                + std::to_string( report.Warnings.size() ) + " warnings); start " + report.Launcher
            : "Build Game FAILED after " + std::to_string( static_cast<int>( report.Seconds ) ) + " s (" + std::to_string( report.Errors.size() ) + " errors)";
        CLog::Log( report.Success ? CLog::LogType::Info : CLog::LogType::Error, summary );
#else
        report.Errors.push_back( "Build Game needs a tools build" );
#endif
        return report;
    }
}
