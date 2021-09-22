#include "AssetBrowser.h"
#include <filesystem>
#include "imgui.h"
#include <stack>
#include "Path.h"
#include "Resource/ResourceCache.h"
#include "Graphics/Texture.h"
#include "File.h"
#include "Utils/StringUtils.h"
#include "Components/Transform.h"
#include "Engine/Engine.h"
#include "Havana.h"
#include "Events/SceneEvents.h"
#include "HavanaEvents.h"
#include "optick.h"
#include "Utils/ImGuiUtils.h"
#include <Utils/CommonUtils.h>
#include <Graphics/ShaderFile.h>
#include <CLog.h>
#include "Utils/PlatformUtils.h"

#if ME_EDITOR

const ImGuiTableSortSpecs* AssetBrowserWidget::AssetDescriptor::s_current_sort_specs = NULL;

AssetBrowserWidget::AssetBrowserWidget(Havana* inEditor)
	: HavanaWidget("Asset Browser", "F2")
	, m_editor(inEditor)
{
	// This class is used for command line asset exporting, so keep this clean.

	AssetDirectory.FullPath = Path("Assets");
	EngineAssetDirectory.FullPath = Path("Engine/Assets");

	ReloadDirectories();
	IsOpen = false;
}

void AssetBrowserWidget::ReloadDirectories()
{
	AssetDirectory.Directories.clear();
	AssetDirectory.Files.clear();
	MasterAssetsList.clear();

	for (auto& file : std::filesystem::recursive_directory_iterator(AssetDirectory.FullPath.FullPath))
	{
		Paths[file.path().string()] = std::filesystem::last_write_time(file);
		ProccessDirectory(file, AssetDirectory);
	}

	EngineAssetDirectory.Directories.clear();
	EngineAssetDirectory.Files.clear();

	if (EngineAssetDirectory.FullPath.Exists)
	{
		for (auto& file : std::filesystem::recursive_directory_iterator(EngineAssetDirectory.FullPath.FullPath))
		{
			Paths[file.path().string()] = std::filesystem::last_write_time(file);
			ProccessDirectory(file, EngineAssetDirectory);
		}
	}
}

AssetBrowserWidget::~AssetBrowserWidget()
{
	IsRunning = false;
	//fileBrowser.join();
}

//void AssetBrowserWidget::ThreadStart(const std::function<void(std::string, FileStatus)>& action)
//{
//	while (IsRunning)
//	{
//		std::this_thread::sleep_for(Delay);
//		continue;
//		bool WasModified = false;
//		auto it = Paths.begin();
//		while (it != Paths.end())
//		{
//			if (!std::filesystem::exists(it->first))
//			{
//				action(it->first, FileStatus::Deleted);
//				it = Paths.erase(it);
//				WasModified = true;
//			}
//			else
//			{
//				it++;
//			}
//		}
//
//		for (auto& file : std::filesystem::recursive_directory_iterator(PathToWatch))
//		{
//			auto currentFileLastWriteTime = std::filesystem::last_write_time(file);
//
//			if (!Contains(file.path().string()))
//			{
//				Paths[file.path().string()] = currentFileLastWriteTime;
//				action(file.path().string(), FileStatus::Created);
//				WasModified = true;
//			}
//			else
//			{
//				if (Paths[file.path().string()] != currentFileLastWriteTime)
//				{
//					Paths[file.path().string()] = currentFileLastWriteTime;
//					action(file.path().string(), FileStatus::Modified);
//					WasModified = true;
//				}
//			}
//		}
//
//		if (WasModified)
//		{
//			Paths.clear();
//			AssetDirectory = Directory();
//			for (auto& file : std::filesystem::recursive_directory_iterator(PathToWatch))
//			{
//				Paths[file.path().string()] = std::filesystem::last_write_time(file);
//				ProccessDirectory(file, AssetDirectory);
//			}
//		}
//	}
//}

void AssetBrowserWidget::Init()
{
	Icons["Image"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Image.png"));
	Icons["File"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/File.png"));
	Icons["Model"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Model.png"));
	Icons["World"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/World.png"));
	Icons["Audio"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Audio.png"));
	Icons["Prefab"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Prefab.png"));

	for (unsigned int i = (unsigned int)AssetType::Unknown + 1; i < (unsigned int)AssetType::Count; i++)
	{
		assetTypeFilters[i] = false;
	}

	std::vector<TypeId> events;
	events.push_back(RequestAssetSelectionEvent::GetEventId());
	EventManager::GetInstance().RegisterReceiver(this, events);
}

void AssetBrowserWidget::Destroy()
{

}

void AssetBrowserWidget::Render()
{
	OPTICK_CATEGORY("Asset Browser", Optick::Category::Debug);
	//if (false) // Old Way, might come back as a viewing setting
	//{
	//	if (ImGui::Begin("Assets", &IsOpen))
	//	{
	//		if (m_compiledAssets.empty())
	//		{
	//			if (ImGui::Button("Build Assets"))
	//			{
	//				BuildAssets();
	//			}
	//			ImGui::SameLine(0.f, 10.f);
	//			if (ImGui::Button("Refresh"))
	//			{
	//				ReloadDirectories();
	//			}
	//		}
	//		else
	//		{
	//			if (ImGui::Button("Clear Assets"))
	//			{
	//				ClearAssets();
	//			}
	//		}
	//		if (ImGui::CollapsingHeader("Game", ImGuiTreeNodeFlags_DefaultOpen))
	//		{
	//			Recursive(AssetDirectory);
	//		}

	//		if (ImGui::CollapsingHeader("Engine"))
	//		{
	//			Recursive(EngineAssetDirectory);
	//		}
	//	}
	//	ImGui::End();
	//}

	if (IsOpen)
	{
		if (ForcedAssetFilter != AssetType::Unknown)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
			ImGui::PushStyleColor(ImGuiCol_Border, { 0.447f, .905f, .39f, .31f });
		}

		ImGui::Begin("Asset Directory", &IsOpen);

		if (ForcedAssetFilter != AssetType::Unknown)
		{
			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor(1);
		}
		{
			static float wOffset = 350.0f;
			static float h = ImGui::GetContentRegionAvail().y;
			float w = 0.f;
			if (!IsMetaPanelOpen)
			{
				w = ImGui::GetContentRegionAvailWidth();
			}
			else
			{
				w = ImGui::GetContentRegionAvailWidth() - wOffset;
			}
			h = ImGui::GetContentRegionAvail().y;
			//ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
			ImGui::BeginChild("child1", ImVec2(w, h), true);
			DrawAssetTable();
			ImGui::EndChild();

			if (IsMetaPanelOpen)
			{
				ImGui::SameLine();
				ImGui::Button("vsplitter", ImVec2(4.0f, h));
				if (ImGui::IsItemHovered())
				{
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
				}
				if (ImGui::IsItemActive())
					wOffset -= ImGui::GetIO().MouseDelta.x;
				ImGui::SameLine();
				ImGui::BeginChild("child2", ImVec2(-1.f, h), true);
				if (metafile)
				{
					metafile->OnEditorInspect();

					if (ImGui::Button("Save"))
					{
						metafile->Save();
						metafile->Export();

						SharedPtr<Resource> res = ResourceCache::GetInstance().GetCached(metafile->FilePath);
						if (res)
						{
							res->Reload();
						}
					}
				}
				ImGui::EndChild();
			}
			else
			{
				TryDestroyMetaFile();
			}
		}

		ImGui::End();
	}
	else
	{
		AssetSelectedCallback = nullptr;
		ForcedAssetFilter = AssetType::Unknown;
		TryDestroyMetaFile();
	}
}

void AssetBrowserWidget::DrawAssetTable()
{
	static ImGuiTableFlags flags =
		ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable
		| ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti
		| ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_NoBordersInBody
		| ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY
		| ImGuiTableFlags_SizingStretchProp;

	static float row_min_height = 16.f; // Auto
	static float inner_width_with_scroll = 0.0f; // Auto-extend
	static bool outer_size_enabled = true;
	static ImGuiTextFilter filter;
	static ImVector<int> selection;
	static bool items_need_sort = false;

	assetTypeFilters[0] = false;

	if (pendingAssetListRefresh)
	{
		ReloadDirectories();
		items_need_filtered = true;
		items_need_sort = true;
		selection.clear();
		SelectedAsset = nullptr;
		pendingAssetListRefresh = false;
	}

	bool stringFilterChanged = filter.Draw("##AssetFilter", ImGui::GetContentRegionAvailWidth() - 100.f);
	bool isAssetTypeForced = ForcedAssetFilter != AssetType::Unknown;
	ImGui::SameLine();
	if (ImGui::Button(IsMetaPanelOpen ? "Details >" : "< Details", {100.f, 18.f}))
	{
		IsMetaPanelOpen = !IsMetaPanelOpen;
		if (IsMetaPanelOpen && SelectedAsset)
		{
			RefreshMetaPanel(SelectedAsset->FullPath);
		}
	}

	if (isAssetTypeForced)
	{
		ImGui::PushStyleColor(ImGuiCol_Header, { 0.447f, .905f, .39f, .31f });
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, { 0.447f, .905f, .39f, .8f });
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, { .06f, .34f, .2f, 1.f });
	}

	if (ImGui::CollapsingHeader(isAssetTypeForced ? "Forced Asset Type Filter Active" : "Filters"))
	{
		for (int i = 1; i < (int)AssetType::Count; ++i)
		{
			if (ImGui::Checkbox(AssetTypeToString((AssetType)i).c_str(), &assetTypeFilters[i]))
			{
				items_need_filtered = true;
			}
		}
	}

	if (isAssetTypeForced)
	{
		ImGui::PopStyleColor(3);
		for (int i = 1; i < (int)AssetType::Count; ++i)
		{
			assetTypeFilters[i] = ForcedAssetFilter == (AssetType)i;
		}
		
		items_need_filtered = true;
	}

	bool allFiltersOff = true;
	for (int i = 1; i < (int)AssetType::Count; ++i)
	{
		allFiltersOff = !assetTypeFilters[i];
		if (!allFiltersOff)
		{
			break;
		}
	}
	items_need_filtered = items_need_filtered || stringFilterChanged;

	bool hasNoTypeFilter = allFiltersOff && ForcedAssetFilter == AssetType::Unknown;

	ImVec2 outer_size_value = ImVec2(-1.f, ImGui::GetWindowHeight() - ImGui::GetCursorPosY());
	const float inner_width_to_use = (flags & ImGuiTableFlags_ScrollX) ? inner_width_with_scroll : 0.0f;
	if (ImGui::BeginTable("##table", 3, flags, outer_size_enabled ? outer_size_value : ImVec2(0, 0), inner_width_to_use))
	{
		ImGui::TableSetupColumn("##ID", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoResize, -1.f, MyItemColumnID_ID);
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, -1.0f, MyItemColumnID_Name);
		ImGui::TableSetupColumn("Local Path", ImGuiTableColumnFlags_WidthStretch, -1.0f, MyItemColumnID_Description);
		ImGui::TableSetupScrollFreeze(1, 1);

		ImGuiTableSortSpecs* sorts_specs = ImGui::TableGetSortSpecs();
		if (sorts_specs && sorts_specs->SpecsDirty)
		{
			items_need_sort = true;
		}
		if (sorts_specs && items_need_sort && MasterAssetsList.size() > 1)
		{
			AssetDescriptor::s_current_sort_specs = sorts_specs; // Store in variable accessible by the sort function.
			qsort(&MasterAssetsList[0], (size_t)MasterAssetsList.size(), sizeof(MasterAssetsList[0]), AssetDescriptor::CompareWithSortSpecs);
			AssetDescriptor::s_current_sort_specs = NULL;
			sorts_specs->SpecsDirty = false;
			items_need_filtered = true;
		}
		items_need_sort = false;

		if (items_need_filtered)
		{
			FilteredAssetList.clear();
			for (auto& it : MasterAssetsList)
			{
				bool passedStringFilter = filter.PassFilter(it.Name.c_str());
				if (passedStringFilter && hasNoTypeFilter)
				{
					FilteredAssetList.push_back(&it);
				}
				else if (passedStringFilter && !hasNoTypeFilter)
				{
					for (int i = 1; i < (int)AssetType::Count; ++i)
					{
						if (it.Type == (AssetType)i && assetTypeFilters[i])
						{
							FilteredAssetList.push_back(&it);
						}
					}
				}
			}
			items_need_filtered = false;
		}

		ImGui::TableHeadersRow();

		ImGui::PushButtonRepeat(true);
		ImGuiListClipper clipper;
		clipper.Begin(FilteredAssetList.size());
		while (clipper.Step())
		{
			for (int row_n = clipper.DisplayStart; row_n < clipper.DisplayEnd; row_n++)
			{
				AssetDescriptor* item = FilteredAssetList[row_n];

				const bool item_is_selected = selection.contains(item->ID);
				ImGui::PushID(item->ID);
				ImGui::TableNextRow(ImGuiTableRowFlags_None, row_min_height);
				bool openFolderShortcut = false;
				bool openFileShortcut = false;

				if (ImGui::TableNextColumn())
				{
					ImGuiSelectableFlags selectable_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap | ImGuiSelectableFlags_AllowDoubleClick;
					if (ImGui::Selectable("##Entry", item_is_selected, selectable_flags, ImVec2(0, row_min_height)))
					{
						SelectedAsset = item;

						openFolderShortcut = (m_editor->GetInput().IsKeyDown(KeyCode::LeftAlt) || m_editor->GetInput().IsKeyDown(KeyCode::RightAlt));
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && AssetSelectedCallback && !openFolderShortcut)
						{
							AssetSelectedCallback(item->FullPath);
							RequestOverlay();
						}
						else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !AssetSelectedCallback && !openFolderShortcut)
						{
							openFileShortcut = true;
						}
						else if (openFolderShortcut)
						{
						}
						else
						{
							if (IsMetaPanelOpen)
							{
								RefreshMetaPanel(item->FullPath);
							}
						}

						if (ImGui::GetIO().KeyCtrl)
						{
							if (item_is_selected)
								selection.find_erase_unsorted(item->ID);
							else
								selection.push_back(item->ID);
						}
						else
						{
							selection.clear();
							selection.push_back(item->ID);
						}
					}

					if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
					{
						//files.FullPath = dir.FullPath;
						ImGui::SetDragDropPayload("DND_ASSET_BROWSER", item, sizeof(AssetDescriptor));
						ImGui::Text(item->Name.c_str());
						ImGui::EndDragDropSource();
					}

					bool deleteFileShortcut = m_editor->GetInput().WasKeyPressed(KeyCode::Delete) && !pendingAssetListRefresh;
					if (ImGui::BeginPopupContextItem("AssetRightClickContext"))
					{
						if (ImGui::MenuItem("Open"))
						{
							openFileShortcut = true;
						}
						if (ImGui::MenuItem("Open Folder", "Alt + LMB"))
						{
							openFolderShortcut = true;
						}
						ImGui::Separator();
						if (ImGui::MenuItem("Delete", "Del"))
						{
							SelectedAsset = item;
							deleteFileShortcut = true;
						}
						ImGui::EndPopup();
					}

					// Shortcuts
					{
						if (openFolderShortcut)
						{
							PlatformUtils::OpenFolder(item->FullPath);
						}
						if (openFileShortcut)
						{
							PlatformUtils::OpenFile(item->FullPath);
						}
						if (SelectedAsset && deleteFileShortcut && !pendingAssetListRefresh)
						{
							PlatformUtils::DeleteFile(SelectedAsset->FullPath);
							pendingAssetListRefresh = true;
						}
					}

					ImGui::SameLine();
					ImVec2 iconSize(16, 16);
					switch (item->Type)
					{
					case AssetType::Level:
						ImGui::Image(Icons["World"]->TexHandle, iconSize);
						break;
					case AssetType::Texture:
						ImGui::Image(Icons["Image"]->TexHandle, iconSize);
						break;
					case AssetType::Model:
						ImGui::Image(Icons["Model"]->TexHandle, iconSize);
						break;
					case AssetType::Audio:
						ImGui::Image(Icons["Audio"]->TexHandle, iconSize);
						break;
					case AssetType::Prefab:
						ImGui::Image(Icons["Prefab"]->TexHandle, iconSize);
						break;
					default:
						ImGui::Image(Icons["File"]->TexHandle, iconSize);
						break;
					}
				}

				if (ImGui::TableNextColumn())
					ImGui::TextUnformatted(item->Name.c_str());

				if(ImGui::TableNextColumn())
					ImGui::Text(item->FullPath.LocalPath.c_str());

				ImGui::PopID();
			}
		}
		ImGui::PopButtonRepeat();

		ImGui::EndTable();
	}

}

void AssetBrowserWidget::RefreshMetaPanel(const Path& item)
{
	TryDestroyMetaFile();
	if (item.Exists)
	{
		SharedPtr<Resource> res = ResourceCache::GetInstance().GetCached(item);
		if (res)
		{
			metafile = res->GetMetadata();
		}
		else
		{
			metafile = ResourceCache::GetInstance().LoadMetadata(item);
		}
		ShouldDelteteMetaFile = !res;
	}
}

void AssetBrowserWidget::RequestOverlay(const std::function<void(Path)> cb, AssetType forcedType)
{
	if (cb)
	{
		IsOpen = true;
	}
	else
	{
		IsOpen = !IsOpen;
	}
	ForcedAssetFilter = forcedType;
	AssetSelectedCallback = cb;
	items_need_filtered = IsOpen;
}

bool AssetBrowserWidget::OnEvent(const BaseEvent& evt)
{
	if (evt.GetEventId() == RequestAssetSelectionEvent::GetEventId())
	{
		const RequestAssetSelectionEvent& event = static_cast<const RequestAssetSelectionEvent&>(evt);
		RequestOverlay(event.Callback, event.ForcedFilter);
	}
	return false;
}

void AssetBrowserWidget::Update()
{

}

void AssetBrowserWidget::Recursive(Directory& dir)
{
	for (auto& directory : dir.Directories)
	{
		bool node_open = ImGui::TreeNode(directory.first.c_str());
		if (ImGui::IsItemClicked())
		{
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_CHILD_TRANSFORM"))
			{
				IM_ASSERT(payload->DataSize == sizeof(ParentDescriptor));
				ParentDescriptor* payload_n = (ParentDescriptor*)payload->Data;

				json prefab;
				SavePrefab(prefab, payload_n->Parent, true);

				File(Path(directory.second.FullPath.Directory + "/" + payload_n->Parent->GetName() + std::string(".prefab"))).Write(prefab[0].dump(4));
			}
			ImGui::EndDragDropTarget();
		}
		if (node_open)
		{
			// we have a subdir
			Recursive(directory.second);
			ImGui::TreePop();
		}
	}
	int i = 0;
	for (AssetDescriptor& files : dir.Files)
	{
		switch (files.Type)
		{
		case AssetType::Level:
			ImGui::Image(Icons["World"]->TexHandle, ImVec2(16, 16));
			break;
		case AssetType::Texture:
			ImGui::Image(Icons["Image"]->TexHandle, ImVec2(16, 16));
			break;
		case AssetType::Model:
			ImGui::Image(Icons["Model"]->TexHandle, ImVec2(16, 16));
			break;
		case AssetType::Audio:
			ImGui::Image(Icons["Audio"]->TexHandle, ImVec2(16, 16));
			break;
		case AssetType::Prefab:
			ImGui::Image(Icons["Prefab"]->TexHandle, ImVec2(16, 16));
			break;
		default:
			ImGui::Image(Icons["File"]->TexHandle, ImVec2(16, 16));
			break;
		}

		ImGui::SameLine();
		//ImGui::Text(files.Name.c_str());
		int node_clicked = -1;
		ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow;
		node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 3));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(-5, 3));
		ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, files.Name.c_str());
		if (ImGui::IsItemClicked())
		{
			SelectedAsset = &files;
			node_clicked = i;
			if (ImGui::IsMouseDoubleClicked(0))
			{
				if (files.FullPath.LocalPath.rfind(".lvl") != std::string::npos)
				{
					LoadSceneEvent evt;
					evt.Level = files.FullPath.LocalPath;
					evt.Fire();
				}
				else
				{
#if ME_PLATFORM_WIN64
					ShellExecute(NULL, L"open", StringUtils::ToWString(SelectedAsset->FullPath.FullPath).c_str(), NULL, NULL, SW_SHOWDEFAULT);
#endif
				}
			}
			else if (ImGui::IsMouseClicked(0))
			{
				//InspectEvent evt;
				//evt.AssetBrowserPath = Path(files.FullPath);
				//evt.Fire();
			}
		}
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
		{
			//files.FullPath = dir.FullPath;
			ImGui::SetDragDropPayload("DND_ASSET_BROWSER", &files, sizeof(AssetDescriptor));
			ImGui::Text(files.Name.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_CHILD_TRANSFORM"))
			{
				IM_ASSERT(payload->DataSize == sizeof(ParentDescriptor));
				ParentDescriptor* payload_n = (ParentDescriptor*)payload->Data;

				json prefab;
				SavePrefab(prefab, payload_n->Parent, true);

				File(Path(files.FullPath.Directory + payload_n->Parent->GetName() + std::string(".prefab"))).Write(prefab[0].dump(4));
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::PopStyleVar(2);
		i++;
	}
}

void AssetBrowserWidget::ProccessDirectory(const std::filesystem::directory_entry& file, Directory& dirRef)
{
	std::string& parentDir = dirRef.FullPath.FullPath;
	std::size_t t = file.path().string().find(parentDir);
	if (t != std::string::npos)
	{
		std::string dir2 = file.path().string().substr(parentDir.size(), file.path().string().size());

		ProccessDirectoryRecursive(dir2, dirRef, file);

	}
}

void AssetBrowserWidget::BuildAssets()
{
	BuildAssetsRecursive(EngineAssetDirectory);
	BuildAssetsRecursive(AssetDirectory);
}

void AssetBrowserWidget::BuildAssetsRecursive(Directory& dir)
{
	for (auto& directory : dir.Directories)
	{
		BuildAssetsRecursive(directory.second);
	}
	for (AssetDescriptor& files : dir.Files)
	{
		switch (files.Type)
		{
		case AssetType::Level:
			break;
		case AssetType::Texture:
			BRUH("Compiling: " + files.FullPath.FullPath);
			m_compiledAssets.push_back(ResourceCache::GetInstance().Get<Moonlight::Texture>(files.FullPath));
			break;
		case AssetType::Model:
			break;
		case AssetType::Audio:
			break;
		case AssetType::Shader:
			BRUH("Compiling: " + files.FullPath.FullPath);
			m_compiledAssets.push_back(ResourceCache::GetInstance().Get<Moonlight::ShaderFile>(files.FullPath));
			break;
		default:
			break;
		}
	}
}

void AssetBrowserWidget::TryDestroyMetaFile()
{
	if (metafile)
	{
		AssetBrowserPath = Path();

		if (ShouldDelteteMetaFile)
		{
			delete metafile;
			ShouldDelteteMetaFile = false;
		}
		metafile = nullptr;
	}
}

void AssetBrowserWidget::ClearAssets()
{
	m_compiledAssets.clear();
}

bool AssetBrowserWidget::ProccessDirectoryRecursive(std::string& dir, Directory& dirRef, const std::filesystem::directory_entry& file)
{
#if ME_PLATFORM_MACOS
	const char slash = '/';
#else
	const char slash = '\\';
#endif
	std::size_t d = dir.find_first_of(slash);
	if (d != std::string::npos)
	{
		std::string newdir = dir.substr(d + 1, dir.length());
		std::size_t d2 = newdir.find_first_of(slash);
		if (d2 != std::string::npos)
		{
			std::string foldername = newdir.substr(0, d2);
			if (dirRef.Directories.find(foldername) != dirRef.Directories.end())
			{
				return ProccessDirectoryRecursive(newdir, dirRef.Directories[foldername], file);
			}
			else
			{
				Directory newDirectory;
				newDirectory.FullPath = Path(newdir);
				dirRef.Directories[foldername] = newDirectory;
				return true;
			}
		}
		else
		{
			if (file.is_regular_file())
			{
				if (newdir.find(".meta") != std::string::npos
					|| newdir.find(".dds") != std::string::npos
					|| newdir.find(".pdn") != std::string::npos
					|| newdir.find(".blend") != std::string::npos
					|| newdir.find(".bin") != std::string::npos
					|| newdir.find(".DS_Store") != std::string::npos)
				{
					return false;
				}
				// we have a file

				AssetType type = AssetType::Unknown;
				if (newdir.rfind(".png") != std::string::npos || newdir.rfind(".jpg") != std::string::npos || newdir.rfind(".tif") != std::string::npos)
				{
					type = AssetType::Texture;
				}
				else if (newdir.rfind(".lvl") != std::string::npos)
				{
					type = AssetType::Level;
				}
				else if (newdir.rfind(".prefab") != std::string::npos)
				{
					type = AssetType::Prefab;
				}
				else if (newdir.rfind(".wav") != std::string::npos || newdir.rfind(".mp3") != std::string::npos)
				{
					type = AssetType::Audio;
				}
				else if (newdir.rfind(".obj") != std::string::npos || newdir.rfind(".fbx") != std::string::npos || newdir.rfind(".FBX") != std::string::npos)
				{
					type = AssetType::Model;
				}
				else if (newdir.rfind(".frag") != std::string::npos || newdir.rfind(".vert") != std::string::npos)
				{
					type = AssetType::Shader;
				}
				else if (newdir.rfind(".html") != std::string::npos)
				{
					type = AssetType::UI;
				}

				AssetDescriptor desc;
				desc.Name = newdir;
				//desc.MetaFile = File(Path(file.path().string() + ".meta"));
				desc.FullPath = Path(file.path().string());
				desc.Type = type;
				dirRef.Files.push_back(desc);
				//const std::string & data = dirRef.Files.back().MetaFile.Read();
				//if (data.empty())
				//{
				//	//dirRef.Files.back().MetaFile.Write("{}");
				//}
				//else
				//{

				//}
				desc.ID = MasterAssetsList.size();
				MasterAssetsList.push_back(desc);
				return true;
			}
			Directory newDirectory;
			newDirectory.FullPath = Path(dir);
			dirRef.Directories[newdir] = newDirectory;
			return true;
		}
	}

	Directory newDirectory;
	newDirectory.FullPath = Path(dir);
	dirRef.Directories[dir] = newDirectory;
	return false;
}

void AssetBrowserWidget::SavePrefab(json& d, Transform* CurrentTransform, bool IsRoot)
{
	json newJson;
	/*json& refJson = d;

	if (!IsRoot)
	{
		refJson = newJson;
	}*/

	newJson["Name"] = CurrentTransform->GetName();

	json& componentsJson = newJson["Components"];
	EntityHandle ent = CurrentTransform->Parent;

	auto comps = ent->GetAllComponents();
	for (auto comp : comps)
	{
		json compJson;
		comp->Serialize(compJson);
		componentsJson.push_back(compJson);
	}
	if (CurrentTransform->GetChildren().size() > 0)
	{
		for (SharedPtr<Transform> Child : CurrentTransform->GetChildren())
		{
			SavePrefab(newJson["Children"], Child.get(), false);
		}
	}

	d.push_back(newJson);
}

bool AssetBrowserWidget::Contains(const std::string& key)
{
	return Paths.find(key) != Paths.end();
}

#endif
