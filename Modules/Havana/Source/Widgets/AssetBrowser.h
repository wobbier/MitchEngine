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
#include "Types/AssetDescriptor.h"

class Transform;
class Resource;
struct MetaBase;
class Havana;

namespace Moonlight {
    class Texture;
}

#if USING( ME_EDITOR )

// Project asset browser: folder tree, grid/list view with texture thumbnails, breadcrumbs and
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

    bool OnEvent( const BaseEvent& evt ) final;

    void Update() override;
    void Render() override;

    // Picker / save dialog. With a callback the browser opens in picking mode; without one it toggles.
    void RequestOverlay( const std::function<void( Path )> cb = nullptr, AssetType forcedType = AssetType::Unknown, bool isRequestingSave = false );

    // Compiles every texture and shader (command line / pre-cooking helper).
    void BuildAssets();
    void ClearAssets();

    static AssetType GetAssetType( const std::string& InPath );

    // Opens the browser on a project folder ("Assets/Models").
    void ShowFolder( const std::string& InFolder );
    // Opens the browser on an asset's folder with the asset selected and its details shown.
    void ShowAsset( const std::string& InAsset );

private:
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
    void DrawFolderTree( const std::string& InPath, const char* InLabel );
    void DrawContents();
    void DrawGrid();
    void DrawList();
    void DrawEntryContextMenu( Entry& InEntry );
    void DrawBackgroundContextMenu();
    void DrawDetails();
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
    std::map<std::string, std::vector<std::string>> m_subfolderCache;
    char m_search[256] = {};
    std::string m_builtSearch;
    AssetType m_typeFilter = AssetType::Unknown;

    // Selection
    std::set<std::string> m_selection;
    std::string m_selectionAnchor;
    std::string m_focusedPath;

    // Rename
    std::string m_renamingPath;
    char m_renameBuffer[256] = {};
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
    SharedPtr<MetaBase> m_metafile = nullptr;
    // A data asset's contents were edited and not saved yet.
    bool m_editableDirty = false;
    bool m_shouldDeleteMetaFile = false;
    SharedPtr<Resource> m_focusedResource = nullptr;
    std::string m_detailsPath;

    // Picker / save dialog
    std::function<void( Path )> m_pickCallback;
    AssetType m_forcedType = AssetType::Unknown;
    bool m_isSaveDialog = false;
    char m_saveName[256] = {};

    // Delete confirmation
    std::vector<std::string> m_pendingDelete;
    bool m_openDeleteConfirmation = false;

    std::vector<SharedPtr<Resource>> m_compiledAssets;
};

#endif
