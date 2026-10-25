#include "PCH.h"
#include "Tools/Cook/Cooker.h"

#include "CLog.h"
#include "Dementia.h"
#include "JSON.h"
#include "Path.h"
#include "VFS/VFS.h"
#include "VFS/Pak.h"
#include "Resource/AssetDatabase.h"
#include "Resource/CookedAssets.h"
#include "Resource/ResourceCache.h"
#include "Engine/ProjectSettings.h"
#include "World/SceneSerializer.h"
#include "Graphics/ModelResource.h"
#include "Graphics/ShaderCompiler.h"
#include "Graphics/ShaderDependencies.h"
#include "Materials/ShaderGraphAsset.h"
#include "Utils/BGFXUtils.h"
#include "Utils/PlatformUtils.h"
#include "Tools/Cook/CookDependencies.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

namespace Cook
{
    namespace
    {
        bool StartsWith( const std::string& InText, const std::string& InPrefix )
        {
            return InText.size() >= InPrefix.size() && InText.compare( 0, InPrefix.size(), InPrefix ) == 0;
        }


        bool EndsWith( const std::string& InText, const std::string& InSuffix )
        {
            return InText.size() >= InSuffix.size() && InText.compare( InText.size() - InSuffix.size(), InSuffix.size(), InSuffix ) == 0;
        }


        // The lower-case extension of the file name ("" when it has none).
        std::string Extension( const std::string& InPath )
        {
            const size_t slash = InPath.find_last_of( '/' );
            const size_t dot = InPath.rfind( '.' );
            if( dot == std::string::npos || ( slash != std::string::npos && dot < slash ) )
            {
                return std::string();
            }
            std::string extension = InPath.substr( dot + 1 );
            std::transform( extension.begin(), extension.end(), extension.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
            return extension;
        }


        std::string FileName( const std::string& InPath )
        {
            const size_t slash = InPath.find_last_of( '/' );
            return slash == std::string::npos ? InPath : InPath.substr( slash + 1 );
        }


        std::string Directory( const std::string& InPath )
        {
            const size_t slash = InPath.find_last_of( '/' );
            return slash == std::string::npos ? std::string() : InPath.substr( 0, slash );
        }


        // JSON assets that can name other assets. Config files are settings, not content: the
        // project's StartupScene only ships when the cook takes its scenes from the project.
        bool IsJsonAsset( const std::string& InPath )
        {
            if( StartsWith( InPath, "Assets/Config/" ) )
            {
                return false;
            }
            const std::string extension = Extension( InPath );
            return extension == "lvl" || extension == "prefab" || extension == "json" || extension == "mat" || extension == "sequence";
        }


        bool LooksLikePath( const std::string& InText )
        {
            return InText.size() < 1024 && InText.find( '/' ) != std::string::npos && InText.find( '.' ) != std::string::npos
                && InText.find( '\n' ) == std::string::npos && InText.find( "://" ) == std::string::npos;
        }


        // A shader graph manifest's program stages ("@Assets/Shaders/Post/Fullscreen" is a project
        // stage, a bare name sits next to the manifest), as ShaderGraphAsset resolves them.
        void CollectGraphStages( const std::string& InManifest, const json& InData, std::vector<std::string>& OutStages )
        {
            if( !InData.contains( "Programs" ) || !InData["Programs"].is_object() )
            {
                return;
            }
            const std::string directory = Directory( InManifest ) + "/";
            for( const json& pair : InData["Programs"] )
            {
                if( !pair.is_array() || pair.size() != 2 || !pair[0].is_string() || !pair[1].is_string() )
                {
                    continue;
                }
                const char* stages[] = { ".vert", ".frag" };
                for( int i = 0; i < 2; ++i )
                {
                    const std::string name = pair[i].get<std::string>();
                    const std::string base = !name.empty() && name[0] == '@' ? name.substr( 1 ) : directory + name;
                    OutStages.push_back( base + stages[i] );
                }
            }
        }


        void WalkJson( const json& InValue, const std::function<void( const std::string& )>& InVisit )
        {
            if( InValue.is_string() )
            {
                const std::string& text = InValue.get_ref<const std::string&>();
                if( !text.empty() && ( text[0] == '{' || text[0] == '[' ) )
                {
                    // JSON inside a string: serialized script fields carry entity/asset references.
                    const json embedded = json::parse( text, nullptr, false );
                    if( !embedded.is_discarded() )
                    {
                        WalkJson( embedded, InVisit );
                        return;
                    }
                }
                InVisit( text );
            }
            else if( InValue.is_array() || InValue.is_object() )
            {
                for( const json& child : InValue )
                {
                    WalkJson( child, InVisit );
                }
            }
        }
    }


    Platform HostPlatform()
    {
#if USING( ME_PLATFORM_WINDOWS )
        return Platform::Windows;
#elif USING( ME_PLATFORM_MACOS )
        return Platform::MacOS;
#else
        return Platform::Linux;
#endif
    }


    std::string PlatformName( Platform InPlatform )
    {
        switch( InPlatform )
        {
        case Platform::Windows: return "windows";
        case Platform::MacOS: return "macos";
        case Platform::Linux:
        default: return "linux";
        }
    }


    bool ParsePlatform( const std::string& InName, Platform& OutPlatform )
    {
        for( Platform platform : { Platform::Linux, Platform::Windows, Platform::MacOS } )
        {
            if( InName == PlatformName( platform ) )
            {
                OutPlatform = platform;
                return true;
            }
        }
        return false;
    }


    ShaderProfile GetShaderProfile( Platform InPlatform )
    {
        switch( InPlatform )
        {
        case Platform::Windows: return { "windows", "s_5_0", "dx11" };
        case Platform::MacOS: return { "osx", "metal", "metal" };
        case Platform::Linux:
        default: return { "linux", "spirv", "spirv" };
        }
    }


    AssetKind Classify( const std::string& InLogicalPath )
    {
        // Dev caches and per-user state.
        if( StartsWith( InLogicalPath, ".tmp/" ) || InLogicalPath.find( "://" ) != std::string::npos )
        {
            return AssetKind::Stripped;
        }
        // Editor-only content: the editor's own art and layout.
        if( StartsWith( InLogicalPath, "Assets/Havana/" ) || StartsWith( InLogicalPath, "Assets/Icons/" ) || FileName( InLogicalPath ) == "imgui.cfg" )
        {
            return AssetKind::Stripped;
        }
        const std::string extension = Extension( InLogicalPath );
        static const std::set<std::string> stripped = {
            "meta", "invalid",                  // import settings (folded into the registry)
            "d", "var", "sh", "sc",             // shader build inputs and depfiles
            "cs", "csproj", "sln",              // scripts ship compiled, next to the executable
            "shadergraph", "edscript", "md",    // editor documents (graphs ship as their .sg/ output)
        };
        if( stripped.count( extension ) )
        {
            return AssetKind::Stripped;
        }
        if( extension == "png" || extension == "jpg" || extension == "jpeg" || extension == "tga" )
        {
            return AssetKind::Texture;
        }
        if( extension == "fbx" || extension == "obj" )
        {
            return AssetKind::Model;
        }
        if( extension == "vert" || extension == "frag" )
        {
            return AssetKind::ShaderStage;
        }
        return AssetKind::Data;
    }


    std::vector<std::string> ShippedFiles( const std::string& InAsset, Platform InPlatform )
    {
        switch( Classify( InAsset ) )
        {
        case AssetKind::Texture: return { InAsset + ".dds" };
        case AssetKind::Model: return { InAsset + ".assbin" };
        case AssetKind::ShaderStage: return { InAsset + "." + GetShaderProfile( InPlatform ).Binary + ".bin" };
        case AssetKind::Data: return { InAsset };
        case AssetKind::Stripped:
        default: return {};
        }
    }


    std::string OwningAsset( const std::string& InRead )
    {
        auto twinOf = [&InRead]( const std::string& InSuffix, AssetKind InKind ) {
            if( !EndsWith( InRead, InSuffix ) )
            {
                return std::string();
            }
            const std::string owner = InRead.substr( 0, InRead.size() - InSuffix.size() );
            return Classify( owner ) == InKind ? owner : std::string();
        };
        if( EndsWith( InRead, ".meta" ) )
        {
            return InRead.substr( 0, InRead.size() - 5 );
        }
        for( const auto& [suffix, kind] : { std::pair<const char*, AssetKind>{ ".dds", AssetKind::Texture }, { ".assbin", AssetKind::Model }, { ".vertexcache", AssetKind::Model } } )
        {
            const std::string owner = twinOf( suffix, kind );
            if( !owner.empty() )
            {
                return owner;
            }
        }
        // "<stage>.<profile>.bin"
        if( EndsWith( InRead, ".bin" ) )
        {
            const std::string withoutBin = InRead.substr( 0, InRead.size() - 4 );
            const size_t dot = withoutBin.rfind( '.' );
            if( dot != std::string::npos && Classify( withoutBin.substr( 0, dot ) ) == AssetKind::ShaderStage )
            {
                return withoutBin.substr( 0, dot );
            }
        }
        return std::string();
    }


    std::string CanonicalAsset( const std::string& InPath )
    {
        if( InPath.empty() || InPath.find( "://" ) != std::string::npos )
        {
            return std::string();
        }
        std::string path = InPath;
        std::replace( path.begin(), path.end(), '\\', '/' );
        VfsResolved resolved = VFS::Get().Resolve( path );
        // Path's legacy rule: a missing engine-assets spelling retries as the overlay path.
        if( !resolved.Exists && StartsWith( path, "Engine/Assets/" ) )
        {
            resolved = VFS::Get().Resolve( path.substr( 7 ) );
        }
        if( !resolved.Exists || !resolved.IsFile )
        {
            return std::string();
        }
        return VFS::RecordedSpelling( resolved );
    }


    namespace
    {
        // The assets one JSON asset names directly.
        std::set<std::string> NamedAssets( const std::string& InAsset )
        {
            std::set<std::string> named;
            std::string text;
            if( !VFS::Get().ReadText( InAsset, text ) )
            {
                return named;
            }
            json data = json::parse( text, nullptr, false );
            if( data.is_discarded() )
            {
                return named;
            }
            // Moved assets: the paths the data names may be stale, its GUID table knows better.
            SceneSerializer::RemapAssetReferences( data, InAsset );
            auto add = [&named]( const std::string& InCanonical ) {
                if( !InCanonical.empty() )
                {
                    named.insert( InCanonical );
                }
            };
            WalkJson( data, [&add]( const std::string& InText ) {
                if( !LooksLikePath( InText ) )
                {
                    return;
                }
                add( CanonicalAsset( InText ) );
                // A shader graph ships as its generated manifest and stages, not the graph.
                if( EndsWith( InText, ".shadergraph" ) )
                {
                    add( CanonicalAsset( Moonlight::ShaderGraphAsset::ManifestPathFor( InText ).GetLocalPathString() ) );
                }
            } );
            if( FileName( InAsset ) == "Material.json" && EndsWith( Directory( InAsset ), ".sg" ) )
            {
                std::vector<std::string> stages;
                CollectGraphStages( InAsset, data, stages );
                for( const std::string& stage : stages )
                {
                    add( CanonicalAsset( stage ) );
                }
            }
            return named;
        }
    }


    void CollectStaticDependencies( const std::string& InAsset, std::set<std::string>& InOutAssets )
    {
        std::vector<std::string> work = { InAsset };
        std::set<std::string> walked;
        while( !work.empty() )
        {
            const std::string asset = work.back();
            work.pop_back();
            if( !walked.insert( asset ).second )
            {
                continue;
            }
            for( const std::string& named : NamedAssets( asset ) )
            {
                InOutAssets.insert( named );
                if( IsJsonAsset( named ) )
                {
                    work.push_back( named );
                }
            }
        }
    }


#if USING( ME_TOOLS )
    namespace
    {
        class Cooker
        {
        public:
            Cooker( const Request& InRequest, Report& OutReport )
                : m_request( InRequest )
                , m_report( OutReport )
                , m_profile( GetShaderProfile( InRequest.TargetPlatform ) )
            {
            }

            void Run()
            {
                AssetDatabase::Get().EnsureScanned();
                const ProjectSettings& settings = ProjectSettings::Get();
                m_excludes = settings.BuildExclude;

                std::vector<std::string> scenes = m_request.Scenes;
                if( scenes.empty() )
                {
                    scenes = settings.BuildScenes;
                    if( !settings.StartupScene.empty() && std::find( scenes.begin(), scenes.end(), settings.StartupScene ) == scenes.end() )
                    {
                        scenes.push_back( settings.StartupScene );
                    }
                }
                if( scenes.empty() )
                {
                    Error( "Nothing to cook: pass --scene, or list scenes in Project Settings > Build" );
                    return;
                }
                for( const std::string& scene : scenes )
                {
                    const std::string canonical = CanonicalAsset( scene );
                    if( canonical.empty() )
                    {
                        Error( "Scene not found: " + scene );
                        continue;
                    }
                    m_scenes.push_back( canonical );
                }
                if( !m_report.Errors.empty() )
                {
                    return;
                }

                GatherDependencies( settings );
                ExpandModels();
                CookTwins();
                if( !m_report.Errors.empty() )
                {
                    return;
                }
                WriteTree();
            }

            // Tools builds cook a source the first time it loads; this does it now for the
            // textures, models and shader stages among InAssets that are missing their twin or
            // stale, so a cook never ships a missing one. Returns how many failed.
            int CookAll( const std::vector<std::string>& InAssets )
            {
                struct Job
                {
                    std::string Asset;
                    SharedPtr<MetaBase> Meta;   // null for shader stages
                };
                // Planned on this thread (metadata loads), run on workers (compiler runs).
                std::vector<Job> jobs;
                for( const std::string& asset : InAssets )
                {
                    const AssetKind kind = Classify( asset );
                    if( kind == AssetKind::ShaderStage )
                    {
                        const std::string source = Path( asset ).FullPath;
                        if( Moonlight::IsShaderBinaryStale( source, source + "." + m_profile.Binary + ".bin" ) )
                        {
                            jobs.push_back( { asset, nullptr } );
                        }
                    }
                    else if( kind == AssetKind::Texture || kind == AssetKind::Model )
                    {
                        SharedPtr<MetaBase> meta = ResourceCache::GetInstance().LoadMetadata( Path( asset ) );
                        if( meta && ( meta->FlaggedForExport || !Path( ShippedFiles( asset, m_request.TargetPlatform ).front() ).Exists ) )
                        {
                            jobs.push_back( { asset, meta } );
                        }
                    }
                }
                std::atomic<size_t> next{ 0 };
                std::mutex reportMutex;
                std::vector<std::thread> workers;
                const unsigned count = std::max( 1u, std::min<unsigned>( 8u, std::thread::hardware_concurrency() ) );
                for( unsigned i = 0; i < count && i < jobs.size(); ++i )
                {
                    workers.emplace_back( [&]() {
                        for( size_t index = next++; index < jobs.size(); index = next++ )
                        {
                            const Job& job = jobs[index];
                            CLog::Log( CLog::LogType::Info, "Cook: cooking " + job.Asset );
                            if( job.Meta )
                            {
                                ResourceCache::GetInstance().ExportIfNeeded( Path( job.Asset ), job.Meta, true );
                                continue;
                            }
                            std::string failure;
                            if( !CompileStage( job.Asset, failure ) )
                            {
                                std::lock_guard<std::mutex> lock( reportMutex );
                                Error( failure );
                            }
                        }
                    } );
                }
                for( std::thread& worker : workers )
                {
                    worker.join();
                }
                int failed = 0;
                for( const std::string& asset : InAssets )
                {
                    const AssetKind kind = Classify( asset );
                    if( ( kind == AssetKind::Texture || kind == AssetKind::Model || kind == AssetKind::ShaderStage ) && !Path( ShippedFiles( asset, m_request.TargetPlatform ).front() ).Exists )
                    {
                        Error( "Couldn't cook " + asset + " for " + PlatformName( m_request.TargetPlatform ) );
                        ++failed;
                    }
                }
                return failed;
            }

        private:
            bool CompileStage( const std::string& InStage, std::string& OutFailure ) const
            {
                if( m_profile.Profile == "s_5_0" && HostPlatform() != Platform::Windows )
                {
                    OutFailure = InStage + ": D3D11 shader binaries compile only on Windows (shaderc's D3D compiler); cook on a Windows host";
                    return false;
                }
                const std::string source = Path( InStage ).FullPath;
                Moonlight::ShaderCompiler::Request request;
                request.SourcePath = source;
                request.OutputPath = source + "." + m_profile.Binary + ".bin";
                request.ShaderStage = Moonlight::ShaderCompiler::StageFromPath( source );
                request.TargetProfile = { m_profile.CompilerPlatform, m_profile.Profile };
                request.WriteDepends = true;
                const Moonlight::ShaderCompiler::Result result = Moonlight::ShaderCompiler::Compile( request );
                if( !result.Success )
                {
                    OutFailure = "Shader compile failed: " + InStage + "\n" + result.Log;
                }
                return result.Success;
            }

            void Error( const std::string& InMessage )
            {
                CLog::Log( CLog::LogType::Error, "Cook: " + InMessage );
                m_report.Errors.push_back( InMessage );
            }


            void Warn( const std::string& InMessage )
            {
                CLog::Log( CLog::LogType::Warning, "Cook: " + InMessage );
                m_report.Warnings.push_back( InMessage );
            }


            bool IsExcluded( const std::string& InPath ) const
            {
                return Classify( InPath ) == AssetKind::Stripped
                    || std::any_of( m_excludes.begin(), m_excludes.end(), [&InPath]( const std::string& InPrefix ) { return !InPrefix.empty() && StartsWith( InPath, InPrefix ); } );
            }


            void AddAsset( const std::string& InAsset )
            {
                if( InAsset.empty() || IsExcluded( InAsset ) || !m_assets.insert( InAsset ).second )
                {
                    return;
                }
                if( IsJsonAsset( InAsset ) )
                {
                    for( const std::string& named : NamedAssets( InAsset ) )
                    {
                        AddAsset( named );
                    }
                }
            }


            // A recorded read: a twin or a .meta stands for its asset; a source the runtime read
            // raw (an RmlUi document's image) ships as those exact bytes.
            void AddRead( const std::string& InRead )
            {
                const std::string owner = OwningAsset( InRead );
                if( !owner.empty() )
                {
                    AddAsset( owner );
                    return;
                }
                if( IsExcluded( InRead ) )
                {
                    return;
                }
                const AssetKind kind = Classify( InRead );
                if( kind == AssetKind::Texture || kind == AssetKind::Model || kind == AssetKind::ShaderStage )
                {
                    m_rawReads.insert( InRead );
                }
                else
                {
                    AddAsset( InRead );
                }
            }


            void AddFileOrFolder( const std::string& InPath )
            {
                const VfsResolved resolved = VFS::Get().Resolve( InPath );
                if( resolved.Exists && !resolved.IsFile )
                {
                    VFS::Get().Enumerate( InPath, true, [this]( const std::string& InFile, bool InIsDirectory ) {
                        if( !InIsDirectory )
                        {
                            const std::string owner = OwningAsset( InFile );
                            AddAsset( owner.empty() ? InFile : owner );
                        }
                    } );
                    return;
                }
                const std::string canonical = CanonicalAsset( InPath );
                if( canonical.empty() )
                {
                    Warn( "Always-included asset not found: " + InPath );
                    return;
                }
                AddAsset( canonical );
            }


            void GatherDependencies( const ProjectSettings& InSettings )
            {
                // What every run reads before any scene: the project's config.
                for( const std::string& config : { std::string( "Assets/Config/ProjectSettings.json" ), std::string( "Assets/Config/Engine.cfg" ),
                         std::string( "Assets/Config/HardwareProfiles.json" ), InSettings.InputActions } )
                {
                    AddAsset( CanonicalAsset( config ) );
                }
                for( const std::string& always : InSettings.BuildAlwaysInclude )
                {
                    AddFileOrFolder( always );
                }
                // The renderer prefetches every engine stage at startup.
                for( const std::string& stage : Moonlight::FindEngineShaderStages() )
                {
                    const std::string canonical = CanonicalAsset( stage );
                    if( !canonical.empty() )
                    {
                        m_engineStages.push_back( canonical );
                        AddAsset( canonical );
                    }
                }
                for( const std::string& scene : m_scenes )
                {
                    AddAsset( scene );
                    // Baked navmeshes default to <Scene>.<Entity>.navmesh next to the scene.
                    const std::string directory = Directory( scene );
                    std::string stem = FileName( scene );
                    stem = stem.substr( 0, stem.rfind( '.' ) ) + ".";
                    VFS::Get().Enumerate( directory, false, [this, &stem]( const std::string& InFile, bool InIsDirectory ) {
                        if( !InIsDirectory && StartsWith( FileName( InFile ), stem ) && EndsWith( InFile, ".navmesh" ) )
                        {
                            AddAsset( InFile );
                        }
                    } );
                }

                std::vector<std::string> recordings = m_request.DependencyFiles;
                if( !m_request.GameExecutable.empty() )
                {
                    for( const std::string& scene : m_scenes )
                    {
                        const std::string recording = Record( scene );
                        if( !recording.empty() )
                        {
                            recordings.push_back( recording );
                        }
                    }
                }
                for( const std::string& recording : recordings )
                {
                    std::vector<std::string> reads;
                    if( !ReadDependencies( recording, reads ) )
                    {
                        Error( "Couldn't read recorded dependencies " + recording );
                        continue;
                    }
                    for( const std::string& read : reads )
                    {
                        AddRead( read );
                    }
                }
            }


            // Plays the scene through the game with --record-deps; returns the recording.
            std::string Record( const std::string& InScene )
            {
                std::string name = InScene;
                std::replace( name.begin(), name.end(), '/', '_' );
                const std::string recording = std::filesystem::absolute( ".tmp/CookRecordings/" + name + ".deps.json" ).generic_string();
                std::error_code error;
                std::filesystem::remove( recording, error );
                const std::string command = "\"" + m_request.GameExecutable + "\" --scene \"" + InScene + "\" --frames " + std::to_string( m_request.RecordFrames )
                    + " --frame-time 0.0166666667 --width 640 --height 360 --no-audio --exit --record-deps \"" + recording + "\"";
                CLog::Log( CLog::LogType::Info, "Cook: recording " + InScene + " through the game" );
                std::string output;
                const int status = PlatformUtils::RunCommand( command, output );
                if( status != 0 || !std::filesystem::exists( recording, error ) )
                {
                    Error( "Recording " + InScene + " failed (exit " + std::to_string( status ) + ")" );
                    return std::string();
                }
                return recording;
            }


            // Models: the textures their materials name, resolved the way the loader does; the
            // answers go into the registry, so cooked runs don't search for near matches.
            void ExpandModels()
            {
                std::vector<std::string> models;
                for( const std::string& asset : m_assets )
                {
                    if( Classify( asset ) == AssetKind::Model )
                    {
                        models.push_back( asset );
                    }
                }
                CookAll( models );
                for( const std::string& model : models )
                {
                    const Path modelPath( model );
                    for( const std::string& reference : ModelResource::ReadTextureReferences( modelPath ) )
                    {
                        const Path found = ModelResource::ResolveTextureReference( modelPath, reference );
                        const std::string texture = found.Exists ? CanonicalAsset( found.GetLocalPathString() ) : std::string();
                        if( texture.empty() )
                        {
                            continue;   // the loader warns about these at runtime as well
                        }
                        AddAsset( texture );
                        m_registry[model]["Textures"][reference] = texture;
                    }
                }
            }


            void CookTwins()
            {
                CookAll( std::vector<std::string>( m_assets.begin(), m_assets.end() ) );
            }


            // The runtime form of everything, the registry and the manifest.
            void WriteTree()
            {
                std::map<std::string, std::string> files;   // shipped logical path -> the asset it serves
                for( const std::string& asset : m_assets )
                {
                    std::vector<std::string> shipped = ShippedFiles( asset, m_request.TargetPlatform );
                    if( Classify( asset ) == AssetKind::Model && Path( asset + ".vertexcache" ).Exists )
                    {
                        shipped.push_back( asset + ".vertexcache" );
                    }
                    for( const std::string& file : shipped )
                    {
                        if( Path( file ).Exists )
                        {
                            files.emplace( file, asset );
                        }
                        else
                        {
                            Warn( "Missing " + file + " (named by the scenes or a recording)" );
                        }
                    }
                    RegisterAsset( asset );
                }
                for( const std::string& read : m_rawReads )
                {
                    files.emplace( read, read );
                }

                const std::filesystem::path out = std::filesystem::absolute( m_request.OutputDirectory );
                if( !PrepareOutput( out ) )
                {
                    return;
                }
                json manifestFiles = json::array();
                for( const auto& [file, asset] : files )
                {
                    std::vector<uint8_t> bytes;
                    if( !VFS::Get().ReadBytes( file, bytes ) )
                    {
                        Error( "Couldn't read " + file );
                        continue;
                    }
                    if( !WriteBytes( out / file, bytes.data(), bytes.size() ) )
                    {
                        Error( "Couldn't write " + ( out / file ).generic_string() );
                        continue;
                    }
                    json entry;
                    entry["Path"] = file;
                    entry["Size"] = bytes.size();
                    if( asset != file )
                    {
                        entry["Asset"] = asset;
                    }
                    manifestFiles.push_back( std::move( entry ) );
                    ++m_report.FileCount;
                    m_report.ByteCount += bytes.size();
                }

                json registry;
                registry["Version"] = 1;
                registry["Platform"] = PlatformName( m_request.TargetPlatform );
                registry["Assets"] = m_registry.empty() ? json::object() : m_registry;
                std::vector<std::string> engineStages;
                for( const std::string& stage : m_engineStages )
                {
                    if( files.count( ShippedFiles( stage, m_request.TargetPlatform ).front() ) )
                    {
                        engineStages.push_back( stage );
                    }
                }
                registry["EngineShaderStages"] = engineStages;

                json manifest;
                manifest["Version"] = 1;
                manifest["Platform"] = PlatformName( m_request.TargetPlatform );
                manifest["Scenes"] = m_scenes;
                manifest["Files"] = std::move( manifestFiles );
                manifest["Bytes"] = m_report.ByteCount;
                const std::string registryText = registry.dump( 1 ) + "\n";
                const std::string manifestText = manifest.dump( 1 ) + "\n";
                if( !WriteBytes( out / CookedAssets::kRegistryPath, registryText.data(), registryText.size() ) || !WriteBytes( out / "CookManifest.json", manifestText.data(), manifestText.size() ) )
                {
                    Error( "Couldn't write the registry or the manifest under " + out.generic_string() );
                    return;
                }
                if( !m_request.PakFile.empty() )
                {
                    WritePak( files, registryText, manifestText );
                }
                m_report.Success = m_report.Errors.empty();
            }


            void RegisterAsset( const std::string& InAsset )
            {
                const AssetKind kind = Classify( InAsset );
                json& entry = m_registry[InAsset];
                if( kind == AssetKind::Texture || kind == AssetKind::Model || kind == AssetKind::ShaderStage )
                {
                    entry["Stripped"] = true;
                }
                std::string metaText;
                if( VFS::Get().ReadText( InAsset + ".meta", metaText ) )
                {
                    json meta = json::parse( metaText, nullptr, false );
                    if( meta.is_object() )
                    {
                        if( meta.contains( "GUID" ) && meta["GUID"].is_string() )
                        {
                            entry["GUID"] = meta["GUID"];
                        }
                        entry["Meta"] = std::move( meta );
                    }
                }
                if( !entry.contains( "GUID" ) )
                {
                    // Prefabs carry their GUID inside; engine content is keyed by its display path.
                    uint64_t guid = AssetDatabase::Get().FindGUID( InAsset );
                    if( guid == 0 )
                    {
                        guid = AssetDatabase::Get().FindGUID( "Engine/" + InAsset );
                    }
                    if( guid != 0 )
                    {
                        entry["GUID"] = SceneSerializer::GUIDToString( guid );
                    }
                }
                if( entry.empty() )
                {
                    m_registry.erase( InAsset );
                }
            }


            bool PrepareOutput( const std::filesystem::path& InOut )
            {
                namespace fs = std::filesystem;
                std::error_code error;
                if( fs::exists( InOut, error ) )
                {
                    // Only ever clear an earlier cook (or an empty directory).
                    const bool isCook = fs::exists( InOut / "CookManifest.json", error );
                    if( !isCook && !fs::is_empty( InOut, error ) )
                    {
                        Error( "Won't replace " + InOut.generic_string() + ": it isn't an earlier cook (no CookManifest.json)" );
                        return false;
                    }
                    fs::remove_all( InOut, error );
                    if( error )
                    {
                        Error( "Couldn't clear " + InOut.generic_string() + " (" + error.message() + ")" );
                        return false;
                    }
                }
                fs::create_directories( InOut, error );
                return !error;
            }


            static bool WriteBytes( const std::filesystem::path& InPath, const void* InBytes, size_t InSize )
            {
                std::error_code error;
                std::filesystem::create_directories( InPath.parent_path(), error );
                std::ofstream stream( InPath, std::ios::out | std::ios::binary | std::ios::trunc );
                stream.write( static_cast<const char*>( InBytes ), static_cast<std::streamsize>( InSize ) );
                return stream.good();
            }


            void WritePak( const std::map<std::string, std::string>& InFiles, const std::string& InRegistry, const std::string& InManifest )
            {
                const bool written = Mpak::Write( m_request.PakFile, [&]( const Mpak::EntrySink& InSink ) {
                    for( const auto& [file, asset] : InFiles )
                    {
                        std::vector<uint8_t> bytes;
                        if( VFS::Get().ReadBytes( file, bytes ) )
                        {
                            InSink( file, bytes.data(), bytes.size() );
                        }
                    }
                    InSink( CookedAssets::kRegistryPath, InRegistry.data(), InRegistry.size() );
                    InSink( "CookManifest.json", InManifest.data(), InManifest.size() );
                } );
                if( !written )
                {
                    Error( "Couldn't write the pak " + m_request.PakFile );
                }
            }

            const Request& m_request;
            Report& m_report;
            ShaderProfile m_profile;
            std::vector<std::string> m_excludes;
            std::vector<std::string> m_scenes;
            std::vector<std::string> m_engineStages;
            std::set<std::string> m_assets;
            std::set<std::string> m_rawReads;
            json m_registry = json::object();
        };
    }
#endif


    Report Run( const Request& InRequest )
    {
        Report report;
#if USING( ME_TOOLS )
        const auto start = std::chrono::steady_clock::now();
        Cooker cooker( InRequest, report );
        cooker.Run();
        const double seconds = std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
        CLog::Log( report.Success ? CLog::LogType::Info : CLog::LogType::Error, "Cook " + std::string( report.Success ? "finished" : "FAILED" ) + ": "
            + std::to_string( report.FileCount ) + " files, " + std::to_string( report.ByteCount / 1024 ) + " KiB for " + PlatformName( InRequest.TargetPlatform )
            + " in " + std::to_string( static_cast<int>( seconds ) ) + " s (" + std::to_string( report.Warnings.size() ) + " warnings, "
            + std::to_string( report.Errors.size() ) + " errors) -> " + InRequest.OutputDirectory );
#else
        report.Errors.push_back( "Cooking needs a tools build" );
#endif
        return report;
    }


    int CookInPlace( const std::vector<std::string>& InRoots, Platform InPlatform )
    {
#if USING( ME_TOOLS )
        Request request;
        request.TargetPlatform = InPlatform;
        Report report;
        Cooker cooker( request, report );
        std::vector<std::string> assets;
        for( const std::string& root : InRoots )
        {
            std::error_code error;
            for( std::filesystem::recursive_directory_iterator it( Path( root ).FullPath, error ), end; it != end; it.increment( error ) )
            {
                if( !it->is_regular_file( error ) )
                {
                    continue;
                }
                const std::string canonical = CanonicalAsset( Path( it->path().generic_string() ).GetLocalPathString() );
                const AssetKind kind = Classify( canonical );
                if( kind == AssetKind::Texture || kind == AssetKind::Model || kind == AssetKind::ShaderStage )
                {
                    assets.push_back( canonical );
                }
            }
        }
        return cooker.CookAll( assets );
#else
        return static_cast<int>( InRoots.size() );
#endif
    }
}
