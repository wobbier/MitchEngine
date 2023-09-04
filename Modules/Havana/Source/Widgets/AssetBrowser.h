#pragma once
#include <Types/AssetType.h>
#include <Events/EventReceiver.h>
#include <HavanaWidget.h>
#include <File.h>
#include <map>
#include <functional>
#include <Pointers.h>
#include <JSON.h>
#include "Editor/AssetDescriptor.h"

class Transform;
class Resource;
struct MetaBase;
class Havana;

namespace Moonlight {
    class Texture;
}

#if USING( ME_EDITOR )

enum MyItemColumnID
{
    MyItemColumnID_ID,
    MyItemColumnID_Name,
    MyItemColumnID_Action,
    MyItemColumnID_Description,
    MyItemColumnID_LastModified
};

enum class FileStatus : unsigned int
{
    Created = 0,
    Modified,
    Deleted
};

class AssetBrowserWidget
    : public HavanaWidget
    , public EventReceiver
{
public:

    struct Directory
    {
        std::map<std::string, Directory> Directories;
        std::vector<AssetDescriptor> Files;
        Path FullPath;
    };
    AssetBrowserWidget( Havana* inEditor );

    void ReloadDirectories();

    ~AssetBrowserWidget();

    void Init() override;
    void Destroy() override;

    bool OnEvent( const BaseEvent& evt ) final;

    void Update() override;
    void Render() override;

    void DrawAssetTable();

    void RequestOverlay( const std::function<void( Path )> cb = nullptr, AssetType forcedType = AssetType::Unknown, bool isRequestingSave = false );
    void Recursive( Directory& dir );
    void ProccessDirectory( const std::filesystem::directory_entry& file, Directory& dirRef );
    void BuildAssets();
    void ClearAssets();
private:
    bool ProccessDirectoryRecursive( std::string& dir, Directory& dirRef, const std::filesystem::directory_entry& file );
    void BuildAssetsRecursive( Directory& dir );

    void RefreshMetaPanel( const Path& item );

    void TryDestroyMetaFile();

    static const ImGuiTableSortSpecs* s_current_sort_specs;
    static int CompareWithSortSpecs( const void* lhs, const void* rhs );

    std::vector<SharedPtr<Resource>> m_compiledAssets;
    void SavePrefab( json& d, Transform* CurrentTransform, bool IsRoot );
    std::unordered_map<std::string, std::filesystem::file_time_type> Paths;
    bool IsRunning = true;
    bool Contains( const std::string& key );
    Directory AssetDirectory;
    Directory EngineAssetDirectory;
    std::unordered_map<std::string, SharedPtr<Moonlight::Texture>> Icons;
    AssetDescriptor* SelectedAsset = nullptr;

    std::vector<AssetDescriptor> MasterAssetsList;
    std::function<void( Path )> AssetSelectedCallback;
    std::vector<AssetDescriptor*> FilteredAssetList;
    AssetType ForcedAssetFilter = AssetType::Unknown;
    bool items_need_filtered = true;
    bool assetTypeFilters[static_cast<unsigned int>( AssetType::Count )];

    Path AssetBrowserPath;
    SharedPtr<MetaBase> metafile = nullptr;
    bool ShouldDelteteMetaFile = false;
    bool IsMetaPanelOpen = false;
    Havana* m_editor = nullptr;
    bool pendingAssetListRefresh = false;
    AssetType CurrentlyFocusedAssetType = AssetType::Unknown;
    SharedPtr<Resource> CurrentlyFocusedAsset = nullptr;
    std::string SavedName;
    bool IsRequestingSave = false;
    bool ImagePreviewActive = false;
    ImVec2 WindowPos;
    ImVec2 WindowSize;
};

#endif
