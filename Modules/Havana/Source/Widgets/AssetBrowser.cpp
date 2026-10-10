#include "AssetBrowser.h"
#include "World/SceneSerializer.h"
#include "Editor/PrefabTools.h"
#include "Editor/EditorActions.h"
#include "Editor/ReflectionUI.h"
#include "Input/InputActions.h"
#include <filesystem>
#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "Path.h"
#include "Resource/ResourceCache.h"
#include "Resource/MetaFile.h"
#include "Graphics/Texture.h"
#include "Graphics/ShaderFile.h"
#include "File.h"
#include "Components/Transform.h"
#include "Engine/Engine.h"
#include "EditorApp.h"
#include "Havana.h"
#include "Events/SceneEvents.h"
#include "Events/HavanaEvents.h"
#include "Events/EditorEvents.h"
#include "optick.h"
#include <Utils/CommonUtils.h>
#include <Utils/EditorConfig.h>
#include <CLog.h>
#include "Utils/PlatformUtils.h"
#include "UI/Colors.h"
#include "Utils/ImGuiUtils.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>

#if USING( ME_EDITOR )

namespace fs = std::filesystem;

namespace
{
    constexpr size_t kMaxThumbnails = 256;
    constexpr const char* kEntityPayload = "DND_CHILD_TRANSFORM";
    constexpr const char* kAssetMovePayload = "DND_ASSET_MOVE";

    std::string ToLower( std::string text )
    {
        std::transform( text.begin(), text.end(), text.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
        return text;
    }

    bool EndsWith( const std::string& text, const std::string& suffix )
    {
        return text.size() >= suffix.size() && text.compare( text.size() - suffix.size(), suffix.size(), suffix ) == 0;
    }

    // Generated / intermediate files never shown in the browser.
    bool IsHiddenFile( const std::string& name )
    {
        static const char* kHidden[] = { ".meta", ".dds", ".assbin", ".pdn", ".blend", ".bin", ".d", ".DS_Store", ".ktx" };
        for( const char* suffix : kHidden )
        {
            if( EndsWith( name, suffix ) )
            {
                return true;
            }
        }
        return !name.empty() && name[0] == '.';
    }

    std::string ToGeneric( const fs::path& path )
    {
        return path.generic_string();
    }

    // Project-local form used for payloads and asset links.
    std::string LocalPath( const std::string& fullPath )
    {
        return Path( fullPath ).GetLocalPathString();
    }

    // The folder containing Assets/ and Engine/ (absolute, generic separators).
    const std::string& ProjectRoot()
    {
        static const std::string root = fs::path( Path( "Assets" ).FullPath ).parent_path().generic_string();
        return root;
    }

    std::string AbsoluteFolder( const std::string& relative )
    {
        return ( fs::path( ProjectRoot() ) / relative ).generic_string();
    }

    // "Engine/Assets/Havana" for display (unambiguous, unlike local asset paths).
    std::string RelativeToProject( const std::string& absolute )
    {
        const std::string& root = ProjectRoot();
        if( absolute.rfind( root, 0 ) == 0 )
        {
            std::string relative = absolute.substr( root.size() );
            while( !relative.empty() && relative.front() == '/' )
            {
                relative.erase( relative.begin() );
            }
            return relative;
        }
        return absolute;
    }

    void DrawFolderGlyph( ImDrawList* drawList, ImVec2 min, float size, ImU32 color )
    {
        const float w = size;
        const float h = size * 0.75f;
        const ImVec2 origin( min.x, min.y + ( size - h ) * 0.5f );
        drawList->AddRectFilled( origin, ImVec2( origin.x + w * 0.45f, origin.y + h * 0.25f ), color, size * 0.06f );
        drawList->AddRectFilled( ImVec2( origin.x, origin.y + h * 0.15f ), ImVec2( origin.x + w, origin.y + h ), color, size * 0.08f );
    }

    std::string FormatModified( int64_t seconds )
    {
        if( seconds <= 0 )
        {
            return {};
        }
        std::time_t time = static_cast<std::time_t>( seconds );
        char buffer[64];
        std::strftime( buffer, sizeof( buffer ), "%Y-%m-%d %H:%M", std::localtime( &time ) );
        return buffer;
    }

    std::string FormatSize( uintmax_t bytes )
    {
        char buffer[32];
        if( bytes >= 1024ull * 1024ull )
        {
            std::snprintf( buffer, sizeof( buffer ), "%.1f MB", bytes / ( 1024.0 * 1024.0 ) );
        }
        else
        {
            std::snprintf( buffer, sizeof( buffer ), "%.1f KB", bytes / 1024.0 );
        }
        return buffer;
    }

    std::string ExtensionFor( AssetType type )
    {
        switch( type )
        {
        case AssetType::Level: return ".lvl";
        case AssetType::Prefab: return ".prefab";
        case AssetType::Material: return ".mat";
        case AssetType::CS: return ".cs";
        default: return {};
        }
    }

    // Moves a file and its .meta sidecar. Returns false on failure.
    bool MoveWithMeta( const std::string& from, const std::string& to )
    {
        std::error_code error;
        fs::rename( from, to, error );
        if( error )
        {
            YIKES( "Couldn't move " + from + " -> " + to + ": " + error.message() );
            return false;
        }
        if( fs::exists( from + ".meta" ) )
        {
            fs::rename( from + ".meta", to + ".meta", error );
        }
        return true;
    }
}


AssetBrowserWidget::AssetBrowserWidget( Havana* inEditor )
    : HavanaWidget( "Asset Browser", "Ctrl+Space" )
    , m_editor( inEditor )
{
    IsOpen = false;
}


AssetBrowserWidget::~AssetBrowserWidget()
{
}


AssetType AssetBrowserWidget::GetAssetType( const std::string& InPath )
{
    const std::string path = ToLower( InPath );
    if( EndsWith( path, ".png" ) || EndsWith( path, ".jpg" ) || EndsWith( path, ".jpeg" ) || EndsWith( path, ".tif" ) || EndsWith( path, ".tga" ) || EndsWith( path, ".hdr" ) )
        return AssetType::Texture;
    if( EndsWith( path, ".lvl" ) )
        return AssetType::Level;
    if( EndsWith( path, ".prefab" ) )
        return AssetType::Prefab;
    if( EndsWith( path, ".wav" ) || EndsWith( path, ".mp3" ) || EndsWith( path, ".ogg" ) )
        return AssetType::Audio;
    if( EndsWith( path, ".obj" ) || EndsWith( path, ".fbx" ) || EndsWith( path, ".gltf" ) || EndsWith( path, ".glb" ) || EndsWith( path, ".dae" ) )
        return AssetType::Model;
    if( EndsWith( path, ".html" ) )
        return AssetType::UI;
    if( EndsWith( path, ".cs" ) )
        return AssetType::CS;
    if( EndsWith( path, ".mat" ) )
        return AssetType::Material;
    if( EndsWith( path, ".shader" ) )
        return AssetType::ShaderGraph;
    if( EndsWith( path, ".vert" ) || EndsWith( path, ".frag" ) || EndsWith( path, ".sh" ) || EndsWith( path, ".sc" ) )
        return AssetType::Shader;
    if( EndsWith( path, ".cpp" ) || EndsWith( path, ".h" ) )
        return AssetType::Code;
    return AssetType::Unknown;
}


void AssetBrowserWidget::Init()
{
    Icons["Image"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/Image.png" ) );
    Icons["File"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/File.png" ) );
    Icons["Model"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/3D.png" ) );
    Icons["World"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/World.png" ) );
    Icons["Audio"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/Audio.png" ) );
    Icons["Prefab"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/Prefab.png" ) );
    Icons["Code"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/Code.png" ) );
    Icons["CS"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/CSharp.png" ) );
    Icons["UI"] = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Havana/UI/UI.png" ) );

    EventManager::GetInstance().RegisterReceiver( this, { RequestAssetSelectionEvent::GetEventId(), AssetsChangedEvent::GetEventId() } );

    EditorConfig& config = EditorConfig::GetInstance();
    m_currentFolder = AbsoluteFolder( config.GetPreference<std::string>( "AssetBrowser.Folder", "Assets" ) );
    if( !fs::is_directory( m_currentFolder ) )
    {
        m_currentFolder = AbsoluteFolder( "Assets" );
    }
    m_showDetails = config.GetPreference<bool>( "AssetBrowser.Details", false );
    Navigate( m_currentFolder );
}


void AssetBrowserWidget::Destroy()
{
    m_thumbnails.clear();
    m_thumbnailOrder.clear();
    Icons.clear();
    TryDestroyMetaFile();
}


bool AssetBrowserWidget::OnEvent( const BaseEvent& evt )
{
    if( evt.GetEventId() == RequestAssetSelectionEvent::GetEventId() )
    {
        const RequestAssetSelectionEvent& event = static_cast<const RequestAssetSelectionEvent&>( evt );
        RequestOverlay( event.Callback, event.ForcedFilter, event.IsRequestingSave );
    }
    else if( evt.GetEventId() == AssetsChangedEvent::GetEventId() )
    {
        const AssetsChangedEvent& event = static_cast<const AssetsChangedEvent&>( evt );
        for( const std::string& path : event.Paths )
        {
            m_thumbnails.erase( ToGeneric( fs::path( path ) ) );
        }
        Refresh();
    }
    return false;
}


void AssetBrowserWidget::Update()
{
}


void AssetBrowserWidget::RequestOverlay( const std::function<void( Path )> cb, AssetType forcedType, bool isRequestingSave )
{
    IsOpen = cb ? true : !IsOpen;
    m_pickCallback = cb;
    m_forcedType = cb ? forcedType : AssetType::Unknown;
    m_isSaveDialog = cb && isRequestingSave;
    m_saveName[0] = '\0';
    m_entriesDirty = true;
    if( IsOpen )
    {
        ImGui::SetWindowFocus( "Asset Directory" );
    }
}


void AssetBrowserWidget::ShowFolder( const std::string& InFolder )
{
    IsOpen = true;
    Navigate( fs::path( InFolder ).is_absolute() ? InFolder : AbsoluteFolder( InFolder ) );
    ImGui::SetWindowFocus( "Asset Directory" );
}


void AssetBrowserWidget::ShowAsset( const std::string& InAsset )
{
    const std::string full = fs::path( InAsset ).is_absolute() ? InAsset : AbsoluteFolder( InAsset );
    ShowFolder( fs::path( full ).parent_path().string() );
    m_selection = { full };
    m_selectionAnchor = full;
    m_showDetails = true;
    SelectForDetails( full );
}


void AssetBrowserWidget::Navigate( const std::string& InFolder, bool InRecordHistory )
{
    std::string folder = InFolder;
    while( folder.size() > 1 && ( folder.back() == '/' || folder.back() == '\\' ) )
    {
        folder.pop_back();
    }
    m_currentFolder = folder;
    if( InRecordHistory )
    {
        if( m_historyIndex + 1 < static_cast<int>( m_history.size() ) )
        {
            m_history.erase( m_history.begin() + m_historyIndex + 1, m_history.end() );
        }
        if( m_history.empty() || m_history.back() != folder )
        {
            m_history.push_back( folder );
        }
        m_historyIndex = static_cast<int>( m_history.size() ) - 1;
    }
    m_entriesDirty = true;
    EditorConfig::GetInstance().SetPreference( "AssetBrowser.Folder", RelativeToProject( folder ) );
}


void AssetBrowserWidget::Refresh()
{
    m_entriesDirty = true;
    m_subfolderCache.clear();
}


bool AssetBrowserWidget::PassesFilters( const Entry& InEntry ) const
{
    const AssetType filter = m_forcedType != AssetType::Unknown ? m_forcedType : m_typeFilter;
    if( !InEntry.IsDirectory && filter != AssetType::Unknown && InEntry.Type != filter )
    {
        return false;
    }
    if( m_search[0] != '\0' && ToLower( InEntry.Name ).find( ToLower( m_search ) ) == std::string::npos )
    {
        return false;
    }
    return true;
}


void AssetBrowserWidget::RebuildEntries()
{
    m_entries.clear();
    m_builtSearch = m_search;
    const fs::path root = m_currentFolder;
    std::error_code error;

    auto addEntry = [this]( const fs::directory_entry& item ) {
        const std::string name = item.path().filename().string();
        std::error_code entryError;
        Entry entry;
        entry.FullPath = ToGeneric( item.path() );
        entry.Name = name;
        entry.IsDirectory = item.is_directory( entryError );
        if( !entry.IsDirectory && IsHiddenFile( name ) )
        {
            return;
        }
        if( entry.IsDirectory && !name.empty() && name[0] == '.' )
        {
            return;
        }
        entry.Type = entry.IsDirectory ? AssetType::Unknown : GetAssetType( name );
        const auto modified = item.last_write_time( entryError );
        if( !entryError )
        {
            entry.Modified = std::chrono::duration_cast<std::chrono::seconds>( modified.time_since_epoch() ).count() + ( std::chrono::duration_cast<std::chrono::seconds>( std::chrono::system_clock::now().time_since_epoch() ).count() - std::chrono::duration_cast<std::chrono::seconds>( fs::file_time_type::clock::now().time_since_epoch() ).count() );
        }
        entry.Size = entry.IsDirectory ? 0 : item.file_size( entryError );
        if( PassesFilters( entry ) )
        {
            m_entries.push_back( std::move( entry ) );
        }
    };

    if( m_search[0] != '\0' )
    {
        // Search covers the current folder recursively; folders themselves aren't listed.
        for( fs::recursive_directory_iterator it( root, fs::directory_options::skip_permission_denied, error ), end; it != end; it.increment( error ) )
        {
            if( !it->is_directory( error ) )
            {
                addEntry( *it );
            }
        }
    }
    else
    {
        for( fs::directory_iterator it( root, error ), end; it != end; it.increment( error ) )
        {
            addEntry( *it );
        }
    }

    std::sort( m_entries.begin(), m_entries.end(), []( const Entry& a, const Entry& b ) {
        if( a.IsDirectory != b.IsDirectory )
        {
            return a.IsDirectory;
        }
        return ToLower( a.Name ) < ToLower( b.Name );
    } );
    m_entriesDirty = false;

    // Drop selections that no longer exist.
    for( auto it = m_selection.begin(); it != m_selection.end(); )
    {
        it = fs::exists( *it ) ? std::next( it ) : m_selection.erase( it );
    }
}


std::vector<std::string> AssetBrowserWidget::ListSubfolders( const std::string& InPath )
{
    auto cached = m_subfolderCache.find( InPath );
    if( cached != m_subfolderCache.end() )
    {
        return cached->second;
    }
    std::vector<std::string> folders;
    std::error_code error;
    for( fs::directory_iterator it( InPath, error ), end; it != end; it.increment( error ) )
    {
        const std::string name = it->path().filename().string();
        if( it->is_directory( error ) && !name.empty() && name[0] != '.' )
        {
            folders.push_back( ToGeneric( it->path() ) );
        }
    }
    std::sort( folders.begin(), folders.end(), []( const std::string& a, const std::string& b ) { return ToLower( a ) < ToLower( b ); } );
    m_subfolderCache[InPath] = folders;
    return folders;
}


SharedPtr<Moonlight::Texture> AssetBrowserWidget::GetIcon( const Entry& InEntry )
{
    switch( InEntry.Type )
    {
    case AssetType::Level: return Icons["World"];
    case AssetType::Texture: return Icons["Image"];
    case AssetType::Model: return Icons["Model"];
    case AssetType::Shader:
    case AssetType::ShaderGraph:
    case AssetType::Code: return Icons["Code"];
    case AssetType::Audio: return Icons["Audio"];
    case AssetType::Prefab: return Icons["Prefab"];
    case AssetType::UI: return Icons["UI"];
    case AssetType::CS: return Icons["CS"];
    default: return Icons["File"];
    }
}


SharedPtr<Moonlight::Texture> AssetBrowserWidget::GetThumbnail( const Entry& InEntry )
{
    if( InEntry.Type != AssetType::Texture )
    {
        return nullptr;
    }
    auto found = m_thumbnails.find( InEntry.FullPath );
    if( found != m_thumbnails.end() )
    {
        return found->second && !found->second->HasLoadFailed() ? found->second : nullptr;
    }
    // Queue a few loads per frame (they load in the background), and only textures that are
    // already compiled (compiling is slow).
    if( m_thumbnailLoadsThisFrame >= 8 )
    {
        return nullptr;
    }
    ++m_thumbnailLoadsThisFrame;
    SharedPtr<Moonlight::Texture> texture;
    Path path( InEntry.FullPath );
    SharedPtr<MetaBase> meta = ResourceCache::GetInstance().LoadMetadata( path );
    const bool compiled = !meta || Path( meta->FilePath.FullPath + "." + meta->GetExtension2() ).Exists;
    if( compiled )
    {
        texture = ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( path );
    }
    m_thumbnails[InEntry.FullPath] = texture;
    m_thumbnailOrder.push_back( InEntry.FullPath );
    while( m_thumbnailOrder.size() > kMaxThumbnails )
    {
        m_thumbnails.erase( m_thumbnailOrder.front() );
        m_thumbnailOrder.pop_front();
    }
    return texture;
}


void AssetBrowserWidget::Render()
{
    OPTICK_CATEGORY( "Asset Browser", Optick::Category::Debug );
    m_thumbnailLoadsThisFrame = 0;
    if( !IsOpen )
    {
        m_pickCallback = nullptr;
        m_forcedType = AssetType::Unknown;
        m_isSaveDialog = false;
        TryDestroyMetaFile();
        return;
    }

    const bool picking = static_cast<bool>( m_pickCallback );
    if( picking )
    {
        ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 1.f );
        ImGui::PushStyleColor( ImGuiCol_Border, { 0.447f, .905f, .39f, .6f } );
    }
    const bool open = ImGui::Begin( "Asset Directory", &IsOpen, ImGuiWindowFlags_NoScrollbar );
    if( picking )
    {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    if( !open )
    {
        ImGui::End();
        return;
    }

    if( m_entriesDirty || m_builtSearch != m_search )
    {
        RebuildEntries();
    }

    if( picking )
    {
        const std::string what = m_forcedType != AssetType::Unknown ? AssetTypeToString( m_forcedType ) : std::string( "asset" );
        ImGui::TextColored( ImVec4( 0.45f, 0.9f, 0.4f, 1.f ), m_isSaveDialog ? "Save %s: pick a folder and a name" : "Select a %s (double-click)", what.c_str() );
    }
    DrawToolbar();

    const float footer = picking ? ImGui::GetFrameHeightWithSpacing() + 6.f : 0.f;
    const float height = std::max( ImGui::GetContentRegionAvail().y - footer, 50.f );
    const float treeWidth = 180.f;
    const float detailsWidth = m_showDetails ? 260.f : 0.f;

    ImGui::BeginChild( "##AssetTree", ImVec2( treeWidth, height ), true );
    DrawFolderTree( AbsoluteFolder( "Assets" ), "Assets" );
    if( fs::is_directory( AbsoluteFolder( "Engine/Assets" ) ) )
    {
        DrawFolderTree( AbsoluteFolder( "Engine/Assets" ), "Engine Assets" );
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild( "##AssetContents", ImVec2( m_showDetails ? ImGui::GetContentRegionAvail().x - detailsWidth - 6.f : 0.f, height ), true );
    DrawContents();
    ImGui::EndChild();

    if( m_showDetails )
    {
        ImGui::SameLine();
        ImGui::BeginChild( "##AssetDetails", ImVec2( 0.f, height ), true );
        DrawDetails();
        ImGui::EndChild();
    }

    if( picking )
    {
        DrawPickerFooter();
    }
    DrawModals();
    ImGui::End();
}


void AssetBrowserWidget::DrawToolbar()
{
    ImGui::BeginDisabled( m_historyIndex <= 0 );
    if( ImGui::ArrowButton( "##Back", ImGuiDir_Left ) )
    {
        Navigate( m_history[--m_historyIndex], false );
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled( m_historyIndex + 1 >= static_cast<int>( m_history.size() ) );
    if( ImGui::ArrowButton( "##Forward", ImGuiDir_Right ) )
    {
        Navigate( m_history[++m_historyIndex], false );
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const fs::path current( m_currentFolder );
    const bool atRoot = m_currentFolder == AbsoluteFolder( "Assets" ) || m_currentFolder == AbsoluteFolder( "Engine/Assets" );
    ImGui::BeginDisabled( atRoot );
    if( ImGui::ArrowButton( "##Up", ImGuiDir_Up ) )
    {
        Navigate( ToGeneric( current.parent_path() ) );
    }
    ImGui::EndDisabled();

    // Breadcrumbs: each segment is a button and a drop target.
    ImGui::SameLine();
    std::string accumulated;
    size_t start = 0;
    const std::string folder = RelativeToProject( m_currentFolder );
    while( start <= folder.size() )
    {
        const size_t slash = folder.find( '/', start );
        const std::string segment = folder.substr( start, slash == std::string::npos ? std::string::npos : slash - start );
        accumulated += ( accumulated.empty() ? "" : "/" ) + segment;
        if( !segment.empty() )
        {
            ImGui::PushID( accumulated.c_str() );
            if( ImGui::SmallButton( segment.c_str() ) && accumulated != "Engine" )
            {
                Navigate( AbsoluteFolder( accumulated ) );
            }
            AcceptDropInto( AbsoluteFolder( accumulated ) );
            ImGui::PopID();
            ImGui::SameLine( 0.f, 2.f );
            if( slash != std::string::npos )
            {
                ImGui::TextDisabled( "/" );
                ImGui::SameLine( 0.f, 2.f );
            }
        }
        if( slash == std::string::npos )
        {
            break;
        }
        start = slash + 1;
    }

    // Right side: search, type filter, view options.
    const float rightWidth = 430.f;
    ImGui::SameLine( std::max( ImGui::GetCursorPosX() + 8.f, ImGui::GetWindowContentRegionMax().x - rightWidth ) );
    ImGui::SetNextItemWidth( 160.f );
    ImGui::InputTextWithHint( "##AssetSearch", "Search (recursive)", m_search, sizeof( m_search ) );
    ImGui::SameLine();
    ImGui::SetNextItemWidth( 100.f );
    ImGui::BeginDisabled( m_forcedType != AssetType::Unknown );
    const AssetType shownFilter = m_forcedType != AssetType::Unknown ? m_forcedType : m_typeFilter;
    if( ImGui::BeginCombo( "##AssetType", shownFilter == AssetType::Unknown ? "All types" : AssetTypeToString( shownFilter ).c_str() ) )
    {
        if( ImGui::Selectable( "All types", m_typeFilter == AssetType::Unknown ) )
        {
            m_typeFilter = AssetType::Unknown;
            m_entriesDirty = true;
        }
        for( unsigned int i = 1; i < static_cast<unsigned int>( AssetType::Count ); ++i )
        {
            const AssetType type = static_cast<AssetType>( i );
            if( ImGui::Selectable( AssetTypeToString( type ).c_str(), m_typeFilter == type ) )
            {
                m_typeFilter = type;
                m_entriesDirty = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    EditorConfig& config = EditorConfig::GetInstance();
    bool listView = config.GetPreference<bool>( "AssetBrowser.ListView", false );
    ImGui::SameLine();
    if( ImGui::SmallButton( listView ? "Grid" : "List" ) )
    {
        config.SetPreference( "AssetBrowser.ListView", !listView );
    }
    if( !listView )
    {
        ImGui::SameLine();
        float tileSize = config.GetPreference<float>( "AssetBrowser.TileSize", 72.f );
        ImGui::SetNextItemWidth( 60.f );
        if( ImGui::SliderFloat( "##TileSize", &tileSize, 40.f, 160.f, "" ) )
        {
            config.SetPreference( "AssetBrowser.TileSize", tileSize );
        }
    }
    ImGui::SameLine();
    if( ImGui::SmallButton( "Refresh" ) )
    {
        Refresh();
        m_thumbnails.clear();
        m_thumbnailOrder.clear();
    }
    ImGui::SameLine();
    if( ImGui::SmallButton( m_showDetails ? "Details >" : "< Details" ) )
    {
        m_showDetails = !m_showDetails;
        config.SetPreference( "AssetBrowser.Details", m_showDetails );
    }
}


void AssetBrowserWidget::DrawFolderTree( const std::string& InPath, const char* InLabel )
{
    const std::string& local = InPath;
    const std::vector<std::string> children = ListSubfolders( InPath );
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if( children.empty() )
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if( local == m_currentFolder )
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    // Keep the branch leading to the current folder open.
    if( m_currentFolder.rfind( local + "/", 0 ) == 0 )
    {
        ImGui::SetNextItemOpen( true, ImGuiCond_Once );
    }
    const std::string label = InLabel ? std::string( InLabel ) : fs::path( InPath ).filename().string();
    const bool open = ImGui::TreeNodeEx( InPath.c_str(), flags, "%s", label.c_str() );
    if( ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() )
    {
        Navigate( local );
    }
    AcceptDropInto( InPath );
    if( open )
    {
        for( const std::string& child : children )
        {
            DrawFolderTree( child, nullptr );
        }
        ImGui::TreePop();
    }
}


bool AssetBrowserWidget::AcceptDropInto( const std::string& InFolder )
{
    if( !ImGui::BeginDragDropTarget() )
    {
        return false;
    }
    bool accepted = false;
    if( ImGui::AcceptDragDropPayload( kAssetMovePayload ) )
    {
        MoveSelectionTo( InFolder );
        accepted = true;
    }
    if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( kEntityPayload ) )
    {
        // Hierarchy entity dropped on a folder: make a prefab of it there.
        const ParentDescriptor* descriptor = static_cast<const ParentDescriptor*>( payload->Data );
        if( descriptor && descriptor->Parent )
        {
            CreatePrefabIn( InFolder, descriptor->Parent );
            Refresh();
        }
        accepted = true;
    }
    ImGui::EndDragDropTarget();
    return accepted;
}


void AssetBrowserWidget::DrawContents()
{
    // The empty area accepts entity drops (prefab creation) into the current folder.
    if( EditorConfig::GetInstance().GetPreference<bool>( "AssetBrowser.ListView", false ) )
    {
        DrawList();
    }
    else
    {
        DrawGrid();
    }

    if( ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
    {
        m_selection.clear();
    }
    DrawBackgroundContextMenu();

    // Shortcuts while the browser has focus.
    if( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && !ImGui::GetIO().WantTextInput )
    {
        if( ImGui::IsKeyPressed( ImGuiKey_Delete ) && !m_selection.empty() )
        {
            m_pendingDelete.assign( m_selection.begin(), m_selection.end() );
            m_openDeleteConfirmation = true;
        }
        if( ImGui::IsKeyPressed( ImGuiKey_F2 ) && m_selection.size() == 1 )
        {
            BeginRename( *m_selection.begin() );
        }
        if( ImGui::IsKeyChordPressed( ImGuiMod_Shortcut | ImGuiKey_D ) && !m_selection.empty() )
        {
            DuplicateSelection();
        }
        if( ImGui::IsKeyPressed( ImGuiKey_Backspace ) && m_historyIndex > 0 )
        {
            Navigate( m_history[--m_historyIndex], false );
        }
    }

    // Dropping on empty space targets the current folder.
    ImGui::SetCursorPos( ImVec2( 0.f, 0.f ) );
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if( available.x > 0.f && available.y > 0.f )
    {
        ImGui::Dummy( ImVec2( ImGui::GetWindowWidth(), std::max( ImGui::GetWindowHeight(), 1.f ) ) );
        AcceptDropInto( m_currentFolder );
    }
}


void AssetBrowserWidget::DrawGrid()
{
    const float tileSize = EditorConfig::GetInstance().GetPreference<float>( "AssetBrowser.TileSize", 72.f );
    const float padding = 10.f;
    const float cellWidth = tileSize + padding;
    const int columns = std::max( 1, static_cast<int>( ImGui::GetContentRegionAvail().x / cellWidth ) );
    const float labelHeight = ImGui::GetTextLineHeight() * 2.f + 4.f;

    if( m_entries.empty() )
    {
        ImGui::TextDisabled( m_search[0] ? "No matches." : "Empty folder. Right-click to create assets." );
    }

    ImGuiListClipper clipper;
    const int rows = ( static_cast<int>( m_entries.size() ) + columns - 1 ) / columns;
    clipper.Begin( rows, tileSize + labelHeight + padding );
    while( clipper.Step() )
    {
        for( int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row )
        {
            for( int column = 0; column < columns; ++column )
            {
                const int index = row * columns + column;
                if( index >= static_cast<int>( m_entries.size() ) )
                {
                    break;
                }
                Entry& entry = m_entries[index];
                if( column > 0 )
                {
                    ImGui::SameLine( 0.f, padding );
                }
                ImGui::PushID( entry.FullPath.c_str() );
                ImGui::BeginGroup();
                const ImVec2 tileMin = ImGui::GetCursorScreenPos();
                const bool selected = m_selection.count( entry.FullPath ) > 0;
                ImGui::InvisibleButton( "##tile", ImVec2( tileSize, tileSize + labelHeight ) );
                const bool hovered = ImGui::IsItemHovered();
                HandleEntryInteraction( entry, index );

                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImVec2 tileMax( tileMin.x + tileSize, tileMin.y + tileSize + labelHeight );
                if( selected || hovered )
                {
                    drawList->AddRectFilled( tileMin, tileMax, selected ? IM_COL32( 0, 112, 224, 110 ) : IM_COL32( 255, 255, 255, 25 ), 4.f );
                }

                // The type icon stands in while a thumbnail loads.
                SharedPtr<Moonlight::Texture> image = GetThumbnail( entry );
                const bool isThumbnail = image && bgfx::isValid( image->TexHandle );
                if( !isThumbnail && !entry.IsDirectory )
                {
                    image = GetIcon( entry );
                }
                if( entry.IsDirectory )
                {
                    const float glyph = tileSize * 0.6f;
                    DrawFolderGlyph( drawList, ImVec2( tileMin.x + ( tileSize - glyph ) * 0.5f, tileMin.y + ( tileSize - glyph ) * 0.5f ), glyph, IM_COL32( 225, 185, 90, 255 ) );
                }
                else if( image && bgfx::isValid( image->TexHandle ) )
                {
                    const float inset = isThumbnail ? 4.f : tileSize * 0.22f;
                    float w = tileSize - inset * 2.f;
                    float h = w;
                    if( isThumbnail && image->mWidth > 0 && image->mHeight > 0 )
                    {
                        const float aspect = static_cast<float>( image->mWidth ) / static_cast<float>( image->mHeight );
                        if( aspect > 1.f ) h = w / aspect; else w = h * aspect;
                    }
                    const ImVec2 imageMin( tileMin.x + ( tileSize - w ) * 0.5f, tileMin.y + ( tileSize - h ) * 0.5f );
                    drawList->AddImage( ImGui::toId( image->TexHandle, IMGUI_FLAGS_ALPHA_BLEND, 0 ), imageMin, ImVec2( imageMin.x + w, imageMin.y + h ) );
                }

                // Label (or inline rename field)
                if( m_renamingPath == entry.FullPath )
                {
                    ImGui::SetCursorScreenPos( ImVec2( tileMin.x, tileMin.y + tileSize + 2.f ) );
                    ImGui::SetNextItemWidth( tileSize );
                    if( m_focusRename )
                    {
                        ImGui::SetKeyboardFocusHere();
                        m_focusRename = false;
                    }
                    const bool commit = ImGui::InputText( "##rename", m_renameBuffer, sizeof( m_renameBuffer ), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll );
                    if( ImGui::IsKeyPressed( ImGuiKey_Escape ) )
                    {
                        m_renamingPath.clear();
                    }
                    else if( commit || ImGui::IsItemDeactivated() )
                    {
                        CommitRename();
                    }
                }
                else
                {
                    const ImVec4 clip( tileMin.x, tileMin.y + tileSize, tileMax.x, tileMax.y );
                    drawList->AddText( ImGui::GetFont(), ImGui::GetFontSize(), ImVec2( tileMin.x + 2.f, tileMin.y + tileSize + 2.f ), IM_COL32( 230, 230, 230, 255 ), entry.Name.c_str(), nullptr, tileSize - 4.f, &clip );
                }
                if( hovered && ImGui::IsMouseHoveringRect( tileMin, tileMax ) )
                {
                    ImGui::SetTooltip( "%s", LocalPath( entry.FullPath ).c_str() );
                }
                ImGui::EndGroup();
                ImGui::PopID();
            }
        }
    }
}


void AssetBrowserWidget::DrawList()
{
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV;
    if( !ImGui::BeginTable( "##AssetList", 4, flags ) )
    {
        return;
    }
    ImGui::TableSetupScrollFreeze( 0, 1 );
    ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthStretch );
    ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 80.f );
    ImGui::TableSetupColumn( "Size", ImGuiTableColumnFlags_WidthFixed, 70.f );
    ImGui::TableSetupColumn( "Modified", ImGuiTableColumnFlags_WidthFixed, 120.f );
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin( static_cast<int>( m_entries.size() ) );
    while( clipper.Step() )
    {
        for( int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index )
        {
            Entry& entry = m_entries[index];
            ImGui::PushID( entry.FullPath.c_str() );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            if( entry.IsDirectory )
            {
                DrawFolderGlyph( ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), 16.f, IM_COL32( 225, 185, 90, 255 ) );
                ImGui::Dummy( ImVec2( 16.f, 16.f ) );
                ImGui::SameLine();
            }
            else if( SharedPtr<Moonlight::Texture> icon = GetIcon( entry ); icon && bgfx::isValid( icon->TexHandle ) )
            {
                ImGui::Image( icon->TexHandle, ImVec2( 16.f, 16.f ) );
                ImGui::SameLine();
            }
            if( m_renamingPath == entry.FullPath )
            {
                if( m_focusRename )
                {
                    ImGui::SetKeyboardFocusHere();
                    m_focusRename = false;
                }
                ImGui::SetNextItemWidth( -1.f );
                const bool commit = ImGui::InputText( "##rename", m_renameBuffer, sizeof( m_renameBuffer ), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll );
                if( ImGui::IsKeyPressed( ImGuiKey_Escape ) )
                {
                    m_renamingPath.clear();
                }
                else if( commit || ImGui::IsItemDeactivated() )
                {
                    CommitRename();
                }
            }
            else
            {
                ImGui::Selectable( entry.Name.c_str(), m_selection.count( entry.FullPath ) > 0, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick );
                HandleEntryInteraction( entry, index );
            }
            ImGui::TableSetColumnIndex( 1 );
            ImGui::TextDisabled( "%s", entry.IsDirectory ? "Folder" : AssetTypeToString( entry.Type ).c_str() );
            ImGui::TableSetColumnIndex( 2 );
            if( !entry.IsDirectory )
            {
                ImGui::TextDisabled( "%s", FormatSize( entry.Size ).c_str() );
            }
            ImGui::TableSetColumnIndex( 3 );
            ImGui::TextDisabled( "%s", FormatModified( entry.Modified ).c_str() );
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}


void AssetBrowserWidget::HandleEntryInteraction( Entry& InEntry, int InIndex )
{
    const ImGuiIO& io = ImGui::GetIO();
    if( ImGui::IsItemClicked( ImGuiMouseButton_Left ) )
    {
        if( io.KeyShift && !m_selectionAnchor.empty() )
        {
            auto anchor = std::find_if( m_entries.begin(), m_entries.end(), [this]( const Entry& e ) { return e.FullPath == m_selectionAnchor; } );
            if( anchor != m_entries.end() )
            {
                int from = static_cast<int>( anchor - m_entries.begin() );
                int to = InIndex;
                if( from > to ) std::swap( from, to );
                if( !io.KeyCtrl ) m_selection.clear();
                for( int i = from; i <= to; ++i )
                {
                    m_selection.insert( m_entries[i].FullPath );
                }
            }
        }
        else if( io.KeyCtrl )
        {
            if( !m_selection.erase( InEntry.FullPath ) )
            {
                m_selection.insert( InEntry.FullPath );
            }
            m_selectionAnchor = InEntry.FullPath;
        }
        else
        {
            m_selection = { InEntry.FullPath };
            m_selectionAnchor = InEntry.FullPath;
        }
        if( !InEntry.IsDirectory )
        {
            SelectForDetails( InEntry.FullPath );
            if( m_isSaveDialog )
            {
                std::snprintf( m_saveName, sizeof( m_saveName ), "%s", fs::path( InEntry.Name ).stem().string().c_str() );
            }
        }
    }
    if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
    {
        OpenEntry( InEntry );
    }
    if( !InEntry.IsDirectory )
    {
        BeginEntryDrag( InEntry );
    }
    else
    {
        // Folders can be dragged (moved) and accept drops.
        BeginEntryDrag( InEntry );
        AcceptDropInto( InEntry.FullPath );
    }
    DrawEntryContextMenu( InEntry );
}


void AssetBrowserWidget::BeginEntryDrag( Entry& InEntry )
{
    if( !ImGui::BeginDragDropSource( ImGuiDragDropFlags_None ) )
    {
        return;
    }
    if( !m_selection.count( InEntry.FullPath ) )
    {
        m_selection = { InEntry.FullPath };
    }
    m_draggedPaths.assign( m_selection.begin(), m_selection.end() );

    // Scene/hierarchy/inspector drops read AssetDescriptor::GetDragged().
    m_dragDescriptor = AssetDescriptor();
    m_dragDescriptor.Name = InEntry.Name;
    m_dragDescriptor.FullPath = Path( InEntry.FullPath );
    m_dragDescriptor.Type = InEntry.Type;
    AssetDescriptor::s_dragged = InEntry.IsDirectory ? nullptr : &m_dragDescriptor;

    // Two payloads aren't possible at once; folders only move, files also feed asset drops.
    if( InEntry.IsDirectory || ImGui::GetIO().KeyAlt )
    {
        ImGui::SetDragDropPayload( kAssetMovePayload, nullptr, 0 );
    }
    else
    {
        ImGui::SetDragDropPayload( AssetDescriptor::kDragAndDropPayload, &m_dragDescriptor.ID, sizeof( int ) );
    }
    if( m_draggedPaths.size() > 1 )
    {
        ImGui::Text( "%zu assets", m_draggedPaths.size() );
    }
    else
    {
        ImGui::Text( "%s", InEntry.Name.c_str() );
    }
    if( !InEntry.IsDirectory )
    {
        ImGui::TextDisabled( "Hold Alt to move into a folder" );
    }
    ImGui::EndDragDropSource();
}


void AssetBrowserWidget::OpenEntry( Entry& InEntry )
{
    if( InEntry.IsDirectory )
    {
        Navigate( InEntry.FullPath );
        return;
    }
    if( m_pickCallback && !m_isSaveDialog )
    {
        std::function<void( Path )> callback = m_pickCallback;
        IsOpen = false;
        m_pickCallback = nullptr;
        callback( Path( InEntry.FullPath ) );
        return;
    }
    if( m_isSaveDialog )
    {
        std::snprintf( m_saveName, sizeof( m_saveName ), "%s", fs::path( InEntry.Name ).stem().string().c_str() );
        return;
    }
    if( InEntry.Type == AssetType::Level )
    {
        if( EditorApp* app = static_cast<EditorApp*>( GetEngine().GetGame() ) )
        {
            app->RequestOpenScene( LocalPath( InEntry.FullPath ) );
        }
        return;
    }
    PlatformUtils::OpenFile( Path( InEntry.FullPath ) );
}


void AssetBrowserWidget::DrawEntryContextMenu( Entry& InEntry )
{
    if( !ImGui::BeginPopupContextItem( "AssetContext" ) )
    {
        return;
    }
    if( !m_selection.count( InEntry.FullPath ) )
    {
        m_selection = { InEntry.FullPath };
    }
    if( m_pickCallback && !m_isSaveDialog && !InEntry.IsDirectory && ImGui::MenuItem( "Select" ) )
    {
        OpenEntry( InEntry );
    }
    if( ImGui::MenuItem( InEntry.IsDirectory ? "Open Folder" : "Open" ) )
    {
        OpenEntry( InEntry );
    }
    if( ImGui::MenuItem( "Show in File Manager" ) )
    {
        PlatformUtils::ShowInFileManager( Path( InEntry.FullPath ) );
    }
    if( ImGui::MenuItem( "Copy Path" ) )
    {
        ImGui::SetClipboardText( LocalPath( InEntry.FullPath ).c_str() );
    }
    ImGui::Separator();
    if( ImGui::MenuItem( "Rename", "F2", false, m_selection.size() == 1 ) )
    {
        BeginRename( InEntry.FullPath );
    }
    if( ImGui::MenuItem( "Duplicate", "Ctrl+D", false, !InEntry.IsDirectory ) )
    {
        DuplicateSelection();
    }
    if( !InEntry.IsDirectory && ImGui::MenuItem( "Reimport" ) )
    {
        if( SharedPtr<MetaBase> meta = ResourceCache::GetInstance().LoadMetadata( Path( InEntry.FullPath ) ) )
        {
            meta->Export();
        }
        ResourceCache::GetInstance().OnFilesChanged( { InEntry.FullPath } );
        m_thumbnails.erase( InEntry.FullPath );
    }
    ImGui::Separator();
    if( ImGui::MenuItem( "Delete", "Del" ) )
    {
        m_pendingDelete.assign( m_selection.begin(), m_selection.end() );
        m_openDeleteConfirmation = true;
    }
    ImGui::EndPopup();
}


void AssetBrowserWidget::DrawBackgroundContextMenu()
{
    if( !ImGui::BeginPopupContextWindow( "AssetBackgroundContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems ) )
    {
        return;
    }
    if( ImGui::BeginMenu( "Create" ) )
    {
        if( ImGui::MenuItem( "Folder" ) )
        {
            CreateFolder();
        }
        ImGui::Separator();
        if( ImGui::MenuItem( "Scene" ) )
        {
            CreateAsset( "New Scene", ".lvl", json{ { "Version", SceneSerializer::kVersion }, { "Cores", json::array() }, { "Entities", json::array() } }.dump( 4 ) );
        }
        if( ImGui::MenuItem( "Prefab" ) )
        {
            const json prefab = { { "Version", SceneSerializer::kVersion }, { "Entities", json::array( { json{ { "GUID", SceneSerializer::GUIDToString( 1 ) }, { "Name", "New Prefab" }, { "Components", json::array( { json{ { "Type", "Transform" } } } ) } } } ) } };
            CreateAsset( "New Prefab", ".prefab", prefab.dump( 4 ) );
        }
        if( ImGui::MenuItem( "Material" ) )
        {
            CreateAsset( "New Material", ".mat", json{ { "Type", "DiffuseMaterial" }, { "DiffuseColor", { 1.0, 1.0, 1.0 } } }.dump( 4 ) );
        }
        if( ImGui::MenuItem( "Input Actions" ) )
        {
            // A starter map: move, look and jump on keyboard / mouse and gamepad.
            InputActionMap map;
            InputContext gameplay;
            gameplay.Name = "Gameplay";
            InputAction move{ "Move", InputActionType::Vector2 };
            InputBinding wasd;
            wasd.Kind = InputBindingKind::Vector2;
            wasd.Up = "Keyboard/W";
            wasd.Down = "Keyboard/S";
            wasd.Left = "Keyboard/A";
            wasd.Right = "Keyboard/D";
            InputBinding leftStick;
            leftStick.Control = "Gamepad/LeftStick";
            move.Bindings = { wasd, leftStick };
            InputAction look{ "Look", InputActionType::Vector2 };
            InputBinding mouse;
            mouse.Control = "Mouse/Delta";
            mouse.Scale = 0.1f;
            InputBinding rightStick;
            rightStick.Control = "Gamepad/RightStick";
            look.Bindings = { mouse, rightStick };
            InputAction jump{ "Jump", InputActionType::Button };
            InputBinding space;
            space.Control = "Keyboard/Space";
            InputBinding south;
            south.Control = "Gamepad/South";
            jump.Bindings = { space, south };
            gameplay.Actions = { move, look, jump };
            map.Contexts = { gameplay };
            json contents;
            Reflection::ToJson( InputActionMap::StaticType(), &map, contents );
            CreateAsset( "New Input Actions", ".inputactions", contents.dump( 4 ) );
        }
        if( ImGui::MenuItem( "C# Script" ) )
        {
            const std::string name = fs::path( UniquePath( m_currentFolder, "NewScript", ".cs" ) ).stem().string();
            const std::string script =
                "using System;\n"
                "using ScriptCore;\n\n"
                "public class " + name + " : Script\n"
                "{\n"
                "    public override void OnStart()\n"
                "    {\n"
                "    }\n\n"
                "    public override void OnUpdate(float dt)\n"
                "    {\n"
                "    }\n"
                "}\n";
            CreateAsset( name, ".cs", script );
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if( ImGui::MenuItem( "Show in File Manager" ) )
    {
        PlatformUtils::OpenFolder( Path( m_currentFolder + "/", true ) );
    }
    if( ImGui::MenuItem( "Refresh" ) )
    {
        Refresh();
    }
    if( ImGui::MenuItem( "Compile All Textures & Shaders" ) )
    {
        BuildAssets();
    }
    ImGui::EndPopup();
}


void AssetBrowserWidget::DrawDetails()
{
    if( m_detailsPath.empty() )
    {
        ImGui::TextDisabled( "Select an asset" );
        return;
    }
    ImGui::TextWrapped( "%s", LocalPath( m_detailsPath ).c_str() );
    std::error_code error;
    if( fs::exists( m_detailsPath, error ) )
    {
        ImGui::TextDisabled( "%s, %s", AssetTypeToString( GetAssetType( m_detailsPath ) ).c_str(), FormatSize( fs::file_size( m_detailsPath, error ) ).c_str() );
    }
    ImGui::Separator();

    if( m_focusedResource )
    {
        if( SharedPtr<Moonlight::Texture> texture = std::dynamic_pointer_cast<Moonlight::Texture>( m_focusedResource ) )
        {
            if( bgfx::isValid( texture->TexHandle ) && texture->mWidth > 0 )
            {
                const float width = ImGui::GetContentRegionAvail().x;
                ImGui::Image( texture->TexHandle, ImVec2( width, width * texture->mHeight / static_cast<float>( texture->mWidth ) ) );
                ImGui::TextDisabled( "%d x %d", static_cast<int>( texture->mWidth ), static_cast<int>( texture->mHeight ) );
            }
        }
    }

    if( m_metafile && m_metafile->GetEditableType() && m_metafile->GetEditableData() )
    {
        // Data asset: its contents are edited in place and written back on Save.
        ImGui::TextUnformatted( "Contents" );
        ReflectionUI::Context context;
        if( ReflectionUI::DrawType( *m_metafile->GetEditableType(), m_metafile->GetEditableData(), context ) )
        {
            m_editableDirty = true;
        }
        if( ImGui::Button( m_editableDirty ? "Save *" : "Save", ImVec2( -1.f, 0.f ) ) )
        {
            m_metafile->SaveEditableData();
            m_editableDirty = false;
        }
        ImGui::Separator();
    }

    if( m_metafile )
    {
        ImGui::TextUnformatted( "Import Settings" );
        m_metafile->OnEditorInspect();
        if( ImGui::Button( "Apply", ImVec2( -1.f, 0.f ) ) )
        {
            m_metafile->Save();
            m_metafile->Export();
            if( m_focusedResource )
            {
                ResourceCache::GetInstance().CompleteLoad( m_focusedResource );
                m_focusedResource->Reload();
            }
            m_thumbnails.erase( m_detailsPath );
        }
    }
}


void AssetBrowserWidget::SelectForDetails( const std::string& InPath )
{
    if( m_detailsPath == InPath )
    {
        return;
    }
    TryDestroyMetaFile();
    m_detailsPath = InPath;
    m_editableDirty = false;
    if( !m_showDetails )
    {
        return;
    }
    Path path( InPath );
    m_focusedResource = ResourceCache::GetInstance().GetCached( path );
    if( !m_focusedResource && GetAssetType( InPath ) == AssetType::Texture )
    {
        m_focusedResource = GetThumbnail( Entry{ InPath, fs::path( InPath ).filename().string(), false, AssetType::Texture } );
    }
    if( m_focusedResource )
    {
        m_metafile = m_focusedResource->GetMetadata();
    }
    else
    {
        m_metafile = ResourceCache::GetInstance().LoadMetadata( path );
        m_shouldDeleteMetaFile = true;
    }
}


void AssetBrowserWidget::TryDestroyMetaFile()
{
    if( m_metafile && m_shouldDeleteMetaFile )
    {
        m_metafile.reset();
    }
    m_metafile = nullptr;
    m_shouldDeleteMetaFile = false;
    m_focusedResource = nullptr;
    m_detailsPath.clear();
}


void AssetBrowserWidget::DrawPickerFooter()
{
    if( m_isSaveDialog )
    {
        ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - 170.f );
        const bool enter = ImGui::InputTextWithHint( "##SaveName", "Name", m_saveName, sizeof( m_saveName ), ImGuiInputTextFlags_EnterReturnsTrue );
        ImGui::SameLine();
        ImGui::BeginDisabled( m_saveName[0] == '\0' );
        if( ImGui::Button( "Save", ImVec2( 80.f, 0.f ) ) || ( enter && m_saveName[0] != '\0' ) )
        {
            std::string name = m_saveName;
            const std::string extension = ExtensionFor( m_forcedType );
            if( !extension.empty() && !EndsWith( ToLower( name ), extension ) )
            {
                name += extension;
            }
            std::function<void( Path )> callback = m_pickCallback;
            IsOpen = false;
            m_pickCallback = nullptr;
            callback( Path( m_currentFolder + "/" + name, true ) );
            Refresh();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if( ImGui::Button( "Cancel", ImVec2( 80.f, 0.f ) ) )
    {
        IsOpen = false;
        m_pickCallback = nullptr;
    }
}


void AssetBrowserWidget::DrawModals()
{
    if( m_openDeleteConfirmation )
    {
        ImGui::OpenPopup( "Delete Assets?" );
        m_openDeleteConfirmation = false;
    }
    if( ImGui::BeginPopupModal( "Delete Assets?", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        ImGui::Text( "Move %zu item(s) (and their .meta files) to .tmp/Trash?", m_pendingDelete.size() );
        for( size_t i = 0; i < std::min<size_t>( m_pendingDelete.size(), 8 ); ++i )
        {
            ImGui::TextDisabled( "%s", LocalPath( m_pendingDelete[i] ).c_str() );
        }
        if( m_pendingDelete.size() > 8 )
        {
            ImGui::TextDisabled( "..." );
        }
        ImGui::Separator();
        if( ImGui::Button( "Delete", ImVec2( 120.f, 0.f ) ) || ImGui::IsKeyPressed( ImGuiKey_Enter ) )
        {
            for( const std::string& path : m_pendingDelete )
            {
                if( !PlatformUtils::MoveToTrash( Path( path ), ".tmp/Trash" ) )
                {
                    YIKES( "Failed to move asset to trash: " + path );
                }
                m_thumbnails.erase( path );
            }
            m_pendingDelete.clear();
            m_selection.clear();
            TryDestroyMetaFile();
            Refresh();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if( ImGui::Button( "Cancel", ImVec2( 120.f, 0.f ) ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) )
        {
            m_pendingDelete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}


std::string AssetBrowserWidget::UniquePath( const std::string& InFolder, const std::string& InName, const std::string& InExtension ) const
{
    std::string candidate = InFolder + "/" + InName + InExtension;
    for( int i = 1; fs::exists( candidate ); ++i )
    {
        candidate = InFolder + "/" + InName + " " + std::to_string( i ) + InExtension;
    }
    return candidate;
}


void AssetBrowserWidget::CreateAsset( const std::string& InName, const std::string& InExtension, const std::string& InContents )
{
    const std::string path = UniquePath( m_currentFolder, InName, InExtension );
    File( Path( path ) ).Write( InContents );
    Refresh();
    RebuildEntries();
    BeginRename( ToGeneric( fs::path( path ) ) );
}


void AssetBrowserWidget::CreateFolder()
{
    const std::string path = UniquePath( m_currentFolder, "New Folder", "" );
    std::error_code error;
    fs::create_directories( path, error );
    Refresh();
    RebuildEntries();
    BeginRename( ToGeneric( fs::path( path ) ) );
}


void AssetBrowserWidget::BeginRename( const std::string& InPath )
{
    m_renamingPath = InPath;
    std::snprintf( m_renameBuffer, sizeof( m_renameBuffer ), "%s", fs::path( InPath ).filename().string().c_str() );
    m_focusRename = true;
    m_selection = { InPath };
}


void AssetBrowserWidget::CommitRename()
{
    const std::string from = m_renamingPath;
    m_renamingPath.clear();
    const std::string newName = m_renameBuffer;
    if( from.empty() || newName.empty() || newName == fs::path( from ).filename().string() || newName.find_first_of( "/\\" ) != std::string::npos )
    {
        return;
    }
    const std::string to = ToGeneric( fs::path( from ).parent_path() / newName );
    if( fs::exists( to ) )
    {
        BRUH( "Can't rename: " + LocalPath( to ) + " already exists." );
        return;
    }
    if( MoveWithMeta( from, to ) )
    {
        m_thumbnails.erase( from );
        m_selection = { to };
        Refresh();
    }
}


void AssetBrowserWidget::DuplicateSelection()
{
    for( const std::string& path : m_selection )
    {
        const fs::path source( path );
        if( fs::is_directory( source ) )
        {
            continue;
        }
        // The copy gets a fresh .meta (and GUID) on first use.
        const std::string destination = UniquePath( ToGeneric( source.parent_path() ), source.stem().string() + " Copy", source.extension().string() );
        std::error_code error;
        fs::copy_file( source, destination, error );
        if( error )
        {
            YIKES( "Duplicate failed: " + error.message() );
        }
    }
    Refresh();
}


void AssetBrowserWidget::MoveSelectionTo( const std::string& InFolder )
{
    const fs::path target( InFolder );
    int moved = 0;
    for( const std::string& path : m_draggedPaths )
    {
        const fs::path source( path );
        if( source.parent_path() == target || target.generic_string().rfind( source.generic_string() + "/", 0 ) == 0 || source == target )
        {
            continue;   // already there, or moving a folder into itself
        }
        const std::string destination = ToGeneric( target / source.filename() );
        if( fs::exists( destination ) )
        {
            BRUH( "Can't move: " + LocalPath( destination ) + " already exists." );
            continue;
        }
        if( MoveWithMeta( path, destination ) )
        {
            ++moved;
        }
    }
    if( moved > 0 )
    {
        BRUH( "Moved " + std::to_string( moved ) + " asset(s). Scenes reference assets by path; re-save scenes that used them." );
    }
    m_draggedPaths.clear();
    m_selection.clear();
    Refresh();
}


void AssetBrowserWidget::CreatePrefabIn( const std::string& InDirectory, Transform* InRoot )
{
    if( !InRoot || !InRoot->Parent )
    {
        return;
    }
    std::string directory = InDirectory;
    while( !directory.empty() && ( directory.back() == '/' || directory.back() == '\\' ) )
    {
        directory.pop_back();
    }
    // The dragged entity becomes an instance of the new prefab.
    const std::string path = UniquePath( directory, InRoot->Parent->GetName(), ".prefab" );
    PrefabTools::CreatePrefab( *InRoot->Parent.Get(), path );
}


void AssetBrowserWidget::BuildAssets()
{
    for( const char* root : { "Engine/Assets", "Assets" } )
    {
        std::error_code error;
        for( fs::recursive_directory_iterator it( Path( root ).FullPath, error ), end; it != end; it.increment( error ) )
        {
            const std::string path = ToGeneric( it->path() );
            const AssetType type = GetAssetType( path );
            if( type == AssetType::Texture )
            {
                BRUH( "Compiling: " + path );
                m_compiledAssets.push_back( ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( path ) ) );
            }
            else if( type == AssetType::Shader && !EndsWith( path, ".sh" ) )
            {
                BRUH( "Compiling: " + path );
                m_compiledAssets.push_back( ResourceCache::GetInstance().Get<Moonlight::ShaderFile>( Path( path ) ) );
            }
        }
    }
}


void AssetBrowserWidget::ClearAssets()
{
    m_compiledAssets.clear();
}

#endif
