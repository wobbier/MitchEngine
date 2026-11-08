#pragma once
#include <HavanaWidget.h>
#include <functional>
#include <unordered_map>
#include <string>
#include <Pointers.h>
#include <Math/Vector2.h>
#include <Path.h>
#include <JSON.h>

class EditorApp;
class Havana;

namespace Moonlight { class Texture; }

class MainMenuWidget
	: public HavanaWidget
{
public:
	MainMenuWidget(Havana* editorApp);

	void Init() final;
	void Destroy() final;

	void Update() final;
	void Render() final;

	void SetData(std::vector<SharedPtr<HavanaWidget>>* widgetList, std::vector<SharedPtr<HavanaWidget>>* customWidgetList, EditorApp* editorApp);

	void SetWindowTitle(const std::string& title);

	void DrawOpenFilePopup();

	Vector2 GetMainMenuSize() const;

	bool MaximizeOnPlay = false;

private:
	EditorApp* App = nullptr;
	std::vector<SharedPtr<HavanaWidget>>* WidgetList = nullptr;
	std::vector<SharedPtr<HavanaWidget>>* CustomWidgetList = nullptr;

	std::unordered_map<std::string, SharedPtr<Moonlight::Texture>> Icons;
	std::string WindowTitle;
	ImVec2 MainMenuSize;
	Vector2 TitleBarDragPosition;
	Vector2 TitleBarDragSize;
	// ED-2: the workspace tabs after the transport controls: click switches, drag reorders,
	// double-click renames, right-click has the rest, + adds one. Narrower than InMaxWidth or a
	// single drop-down of them.
	void DrawWorkspaceTabs( float InHeight, float InMaxWidth );
	char m_workspaceName[64] = {};
	int m_workspaceRenameFocus = 0;     // the workspace whose rename field takes focus next frame
	int m_workspaceMenuFor = 0;
	int m_workspaceRenaming = 0;
	bool m_workspaceTabDragged = false;
	bool ShowDemoWindow = false;
	bool ShowAboutWindow = false;
	json AssetDirectory;
	Havana* Editor = nullptr;

#if USING( ME_FMOD )
	SharedPtr<Moonlight::Texture> FMODImage;
#endif

#if USING( ME_SCRIPTING )
    SharedPtr<Moonlight::Texture> DOTNETImage;
#endif

	void BrowseDirectory(const Path& path);
};