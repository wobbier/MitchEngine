#pragma once
#include <Types/AssetType.h>
#include <Events/EventReceiver.h>
#include <HavanaWidget.h>
#include <File.h>
#include <map>
#include <set>
#include <functional>
#include <Pointers.h>
#include <JSON.h>
#include <list>
#include <unordered_map>
#include <memory>
#include "Types/AssetDescriptor.h"

class Transform;
class Resource;
struct MetaBase;
class Havana;
class ShaderGraphPreview;
namespace ShaderGraph { class Graph; }

namespace Moonlight {
    class Texture;
}

#if USING( ME_EDITOR )

// Project asset browser: searchable project table by default, optional folder grid/list with
// history, recursive search and type filter, multi-select, file operations (create, rename,
// duplicate, move by drag, delete to .tmp/Trash, show in file manager), import settings, and the
// asset picker / save dialog used by RequestAssetSelectionEvent. Refreshes when files change.
class AssetBrowserWidget
    : public HavanaWidget
    , public EventReceiver
{
public:
    AssetBrowserWidget( Havana* inEditor );
    ~AssetBrowserWidget();

    void Init() override;
    void Destroy() override;

    void Update() override;
    void Render() override;

    // Picker / save dialog. With a callback the browser opens in picking mode; without one it toggles.
    void RequestOverlay( const std::function<void( Path )> cb = nullptr, AssetType forcedType = AssetType::Unknown, bool isRequestingSave = false, bool imagesOnly = false );

    // Compiles every texture and shader (command line / pre-cooking helper).
    // Cooks every texture, model and shader in place (Cook::CookInPlace).
    void BuildAssets();

    static AssetType GetAssetType( const std::string& InPath );

    // Opens the browser on a project folder ("Assets/Models").
    void ShowFolder( const std::string& InFolder );
    // Opens the browser on an asset's folder with the asset selected and its details shown.
    void ShowAsset( const std::string& InAsset );

private:
    friend class TerrainFileAutomation;
    friend class EditorAutomation;

    struct Entry
    {
        std::string FullPath;
        std::string Name;
        bool IsDirectory = false;
        AssetType Type = AssetType::Unknown;
        int64_t Modified = 0;
        uintmax_t Size = 0;
    };

    void DrawToolbar();
    void DrawProjectToolbar();
    void DrawProjectFilters();
    void ToggleDetails();
    void DrawFolderTree( const std::string& InPath, const char* InLabel );
    void DrawContents();
    void DrawGrid();
    void DrawList();
    void DrawProjectList();
    void DrawEntryContextMenu( Entry& InEntry );
    void DrawBackgroundContextMenu();
    void DrawCreateMenuItems();
    void DrawDetails();
    void DrawShaderGraphDetails();
    void DrawTreeDetails();
    void DrawPickerFooter();
    void DrawModals();

    void HandleEntryInteraction( Entry& InEntry, int InIndex );
    void BeginEntryDrag( Entry& InEntry );
    bool AcceptDropInto( const std::string& InFolder );
    void OpenEntry( Entry& InEntry );

    void Navigate( const std::string& InFolder, bool InRecordHistory = true );
    void Refresh();
    void RebuildEntries();
    bool PassesFilters( const Entry& InEntry ) const;
    std::vector<std::string> ListSubfolders( const std::string& InPath );

    // File operations
    std::string UniquePath( const std::string& InFolder, const std::string& InName, const std::string& InExtension ) const;
    void CreateAsset( const std::string& InName, const std::string& InExtension, const std::string& InContents );
    void CreateFolder();
    void BeginRename( const std::string& InPath );
    void CommitRename();
    void DuplicateSelection();
    void MoveSelectionTo( const std::string& InFolder );
    void CreatePrefabIn( const std::string& InDirectory, Transform* InRoot );

    SharedPtr<Moonlight::Texture> GetThumbnail( const Entry& InEntry );
    SharedPtr<Moonlight::Texture> GetIcon( const Entry& InEntry );
    void SelectForDetails( const std::string& InPath );
    void TryDestroyMetaFile();

    Havana* m_editor = nullptr;
    std::unordered_map<std::string, SharedPtr<Moonlight::Texture>> Icons;

    // Navigation
    std::string m_currentFolder = "Assets";
    std::vector<std::string> m_history;
    int m_historyIndex = -1;

    // Contents of the current folder (or search results)
    std::vector<Entry> m_entries;
    bool m_entriesDirty = true;
    bool m_sortDirty = true;
    bool m_folderView = false;
    ImGuiTextFilter m_projectFilter;
    std::set<AssetType> m_projectTypes;
    float m_filterHeight = 140.f;
    std::map<std::string, std::vector<std::string>> m_subfolderCache;
    char m_search[256] = {};
    std::string m_builtSearch;
    AssetType m_typeFilter = AssetType::Unknown;

    // Selection
    std::set<std::string> m_selection;
    std::string m_selectionAnchor;
    bool m_revealSelection = false;
    std::string m_focusedPath;

    // Rename
    std::string m_renamingPath;
    char m_renameBuffer[256] = {};
    std::string m_fileOperationError;
    bool m_focusRename = false;

    // Dragging
    AssetDescriptor m_dragDescriptor;
    std::vector<std::string> m_draggedPaths;

    // Thumbnails (bounded, loaded a couple per frame)
    std::unordered_map<std::string, SharedPtr<Moonlight::Texture>> m_thumbnails;
    std::list<std::string> m_thumbnailOrder;
    int m_thumbnailLoadsThisFrame = 0;

    // Details / import settings
    bool m_showDetails = false;
    float m_detailsWidth = 350.f;
    SharedPtr<MetaBase> m_metafile = nullptr;
    // A data asset's contents were edited and not saved yet.
    bool m_editableDirty = false;
    bool m_shouldDeleteMetaFile = false;
    SharedPtr<Resource> m_focusedResource = nullptr;
    std::string m_detailsPath;
    std::unique_ptr<ShaderGraph::Graph> m_previewGraph;
    std::unique_ptr<ShaderGraphPreview> m_graphPreview;
    bool m_graphPreviewDirty = true;
    std::string m_graphPreviewError;
    // The selected tree recipe's summary, read again when the file changes (path + write time).
    std::string m_treeInfoKey;
    const char* m_treeIcon = "tree";
    std::string m_treeInfo;

    // Picker / save dialog
    std::function<void( Path )> m_pickCallback;
    AssetType m_forcedType = AssetType::Unknown;
    // A forced Texture filter that leaves out render textures (RequestAssetSelectionEvent::ImagesOnly).
    bool m_imagesOnly = false;
    bool m_isSaveDialog = false;
    char m_saveName[256] = {};

    // Delete confirmation
    std::vector<std::string> m_pendingDelete;
    bool m_openDeleteConfirmation = false;

};

#endif
