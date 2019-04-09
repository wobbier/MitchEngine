#include "Havana.h"
#include "imgui.h"
#include "examples/imgui_impl_win32.h"
#include "examples/imgui_impl_dx11.h"
#include "Engine/World.h"
#include "Components/Transform.h"
#include <stack>
#include "Components/Camera.h"
#include "ECS/Core.h"
#include "Engine/Engine.h"
#include "HavanaEvents.h"
#include <string>
#include <iostream>
#include "Graphics/RenderTexture.h"
#if ME_EDITOR
#include <filesystem>
#include "Math/Vector2.h"
#include "Mathf.h"
namespace fs = std::filesystem;

Havana::Havana(Engine* GameEngine, Moonlight::Renderer* renderer)
	: Renderer(renderer)
	, m_engine(GameEngine)
	, CurrentDirectory("Assets")
{
	InitUI();
}

void Havana::InitUI()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	//io.MouseDrawCursor = true;

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}
	ImGui_ImplWin32_Init(Renderer->GetDevice().m_window);
	ImGui_ImplDX11_Init(Renderer->GetDevice().GetD3DDevice(), Renderer->GetDevice().GetD3DDeviceContext());
}

bool show_demo_window = true;
void Havana::NewFrame(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc)
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGuiIO& io = ImGui::GetIO();

	// Output size fix?
	RenderSize = Vector2(io.DisplaySize.x, io.DisplaySize.y);
	Renderer->GetDevice().SetOutputSize(RenderSize);

	DrawMainMenuBar();
	DrawOpenFilePopup();

	static float f = 0.0f;
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowBgAlpha(0.0f);

	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
	window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
	window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("DockSpace Demo", &show_demo_window, window_flags);
	ImGui::PopStyleVar(3);

	ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
	ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

	ImGui::End();

	ImGui::Begin("Debug Info");

	ImGui::Checkbox("Demo Window", &show_demo_window);
	ImGui::ShowDemoWindow(&show_demo_window);

	if (ImGui::Button("Play"))
	{
		StartGameFunc();

	}

	if (m_engine->IsGameRunning())
	{
		if (ImGui::Button("Pause"))
		{
			PauseGameFunc();
		}

		if (ImGui::Button("Stop"))
		{
			StopGameFunc();
		}
	}

	ImGui::Text("FPS Average: %.3f FPS: (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	ImGui::End();
}

void Havana::DrawOpenFilePopup()
{
	if (OpenScene)
	{
		ImGui::OpenPopup("Test");
	}
	if (ImGui::BeginPopupModal("Test", &OpenScene, ImGuiWindowFlags_MenuBar))
	{
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Dummy menu item")) {}
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		BrowseDirectory(FilePath("Assets"));

		for (auto& j : AssetDirectory)
		{
			FilePath path(j);
			ImGui::TreeNode(path.LocalPath.c_str());
			if (ImGui::IsItemClicked())
			{
				if (!j.is_string())
				{
					return;
				}
				LoadSceneEvent evt;
				evt.Level = path.LocalPath;
				evt.Fire();
				OpenScene = false;
				ImGui::CloseCurrentPopup();
			}
		}

		if (ImGui::Button("Close"))
		{
			OpenScene = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void Havana::DrawMainMenuBar()
{
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			//ImGui::MenuItem("(dummy menu)", NULL, false, false);
			if (ImGui::MenuItem("New Scene"))
			{
				NewSceneEvent evt;
				evt.Queue();
			}
			if (ImGui::MenuItem("Open Scene", "Ctrl+O"))
			{
				OpenScene = true;
			}

			if (ImGui::BeginMenu("Open Recent"))
			{
				ImGui::MenuItem("fish_hat.c");
				ImGui::MenuItem("fish_hat.inl");
				ImGui::MenuItem("fish_hat.h");
				if (ImGui::BeginMenu("More.."))
				{
					ImGui::MenuItem("Hello");
					ImGui::MenuItem("Sailor");
					if (ImGui::BeginMenu("Recurse.."))
					{
						//ShowExampleMenuFile();
						ImGui::EndMenu();
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Save", "Ctrl+S"))
			{
				SaveSceneEvent evt;
				evt.Fire();
			}
			if (ImGui::MenuItem("Save As..")) {}
			ImGui::Separator();
			if (ImGui::BeginMenu("Options"))
			{
				static bool enabled = true;
				ImGui::MenuItem("Enabled", "", &enabled);
				ImGui::BeginChild("child", ImVec2(0, 60), true);
				for (int i = 0; i < 10; i++)
					ImGui::Text("Scrolling Text %d", i);
				ImGui::EndChild();
				static float f = 0.5f;
				static int n = 0;
				static bool b = true;
				ImGui::SliderFloat("Value", &f, 0.0f, 1.0f);
				ImGui::InputFloat("Input", &f, 0.1f);
				ImGui::Combo("Combo", &n, "Yes\0No\0Maybe\0\0");
				ImGui::Checkbox("Check", &b);
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Quit", "Alt+F4")) {}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Undo", "CTRL+Z")) {}
			if (ImGui::MenuItem("Redo", "CTRL+Y", false, false)) {}  // Disabled item
			ImGui::Separator();
			if (ImGui::MenuItem("Cut", "CTRL+X")) {}
			if (ImGui::MenuItem("Copy", "CTRL+C")) {}
			if (ImGui::MenuItem("Paste", "CTRL+V")) {}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Add"))
		{
			if (ImGui::MenuItem("Entity"))
			{
				SelectedEntity = m_engine->GetWorld().lock()->CreateEntity().lock().get();
				SelectedEntity->AddComponent<Transform>("New Entity");
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
}

void Havana::AddComponentPopup()
{
	{
		static int selected_fish = -1;
		const char* names[] = { "Bream", "Haddock", "Mackerel", "Pollock", "Tilefish" };
		static bool toggles[] = { true, false, false, false, false };

		// Simple selection popup
		// (If you want to show the current selection inside the Button itself, you may want to build a string using the "###" operator to preserve a constant ID with a variable label)
		ImGui::PushItemWidth(-100);
		if (ImGui::Button("Add Component.."))
		{
			ImGui::OpenPopup("my_select_popup");
		}
		ImGui::PopItemWidth();

		if (ImGui::BeginPopup("my_select_popup"))
		{
			ImGui::Text("Components");
			ImGui::Separator();

			ComponentRegistry& reg = GetComponentRegistry();

			for (auto& thing : reg)
			{
				if (ImGui::Selectable(thing.first.c_str()))
				{
					if (SelectedEntity)
					{
						SelectedEntity->AddComponentByName(thing.first);
					}
					if (SelectedTransform)
					{
						m_engine->GetWorld().lock()->GetEntity(SelectedTransform->Parent)->AddComponentByName(thing.first);
					}
				}
			}
			ImGui::EndPopup();
		}
	}
}

void Havana::UpdateWorld(World * world, Transform * root, const std::vector<Entity> & ents)
{
	ImGui::Begin("World");
	if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
	{
		UpdateWorldRecursive(root);
	}
	if (ents.size() > 0)
	{
		if (ImGui::CollapsingHeader("Utility", ImGuiTreeNodeFlags_DefaultOpen))
		{
			int i = 0;
			for (const Entity& ent : ents)
			{
				for (BaseComponent* comp : ent.GetAllComponents())
				{
					ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | (SelectedEntity == &ent ? ImGuiTreeNodeFlags_Selected : 0);
					{
						node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
						ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, comp->GetName().c_str());
						if (ImGui::IsItemClicked())
						{
							SelectedCore = nullptr;
							SelectedTransform = nullptr;
							SelectedEntity = &const_cast<Entity&>(ent);
						}
					}
				}
			}
		}
	}

	if (ImGui::CollapsingHeader("Entity Cores", ImGuiTreeNodeFlags_DefaultOpen))
	{
		int i = 0;
		for (BaseCore* comp : world->GetAllCores())
		{
			ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | (SelectedCore == comp ? ImGuiTreeNodeFlags_Selected : 0);
			{
				node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
				ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, comp->GetName().c_str());
				if (ImGui::IsItemClicked())
				{
					SelectedCore = comp;
					SelectedTransform = nullptr;
					SelectedEntity = nullptr;
				}
			}
		}
	}
	ImGui::End();

	ImGui::Begin("Properties");

	Entity* entity = SelectedEntity;
	if (SelectedTransform != nullptr)
	{
		entity = world->GetEntity(SelectedTransform->Parent);
	}

	if (entity)
	{
		for (BaseComponent* comp : entity->GetAllComponents())
		{
			if (ImGui::CollapsingHeader(comp->GetName().c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				comp->OnEditorInspect();
			}
		}
		AddComponentPopup();
	}

	if (SelectedCore != nullptr)
	{
		SelectedCore->OnEditorInspect();
	}
	ImGui::End();
}

void Havana::UpdateWorldRecursive(Transform * root)
{
	if (!root)
		return;
	int i = 0;
	for (Transform* var : root->Children)
	{
		ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | (SelectedTransform == var ? ImGuiTreeNodeFlags_Selected : 0);
		if (var->Children.empty())
		{
			node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
			ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, var->Name.c_str());
			if (ImGui::IsItemClicked())
			{
				SelectedTransform = var;
				SelectedCore = nullptr;
				SelectedEntity = m_engine->GetWorld().lock()->GetEntity(SelectedTransform->Parent);
			}
		}
		else
		{
			bool node_open = ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, var->Name.c_str());
			if (ImGui::IsItemClicked())
			{
				SelectedTransform = var;
				SelectedCore = nullptr;
				SelectedEntity = m_engine->GetWorld().lock()->GetEntity(SelectedTransform->Parent);
			}

			if (node_open)
			{
				UpdateWorldRecursive(var);
				ImGui::TreePop();
			}
		}

		i++;
	}
}

void Havana::Render()
{
	//ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	//ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	//ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("Game");
	{
		m_isGameFocused = ImGui::IsWindowFocused();

		//ImGui::PopStyleVar(3);

		if (Renderer->RTT->shaderResourceViewMap != nullptr)
		{
			// Get the current cursor position (where your window is)
			ImVec2 pos = ImGui::GetCursorScreenPos();
			ImVec2 maxPos = ImVec2(pos.x + ImGui::GetWindowSize().x, pos.y + ImGui::GetWindowSize().y);
			//GameRenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
			GameRenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);

			// Ask ImGui to draw it as an image:
			// Under OpenGL the ImGUI image type is GLuint
			// So make sure to use "(void *)tex" but not "&tex"
			ImGui::GetWindowDrawList()->AddImage(
				(void*)Renderer->RTT->shaderResourceViewMap,
				ImVec2(pos.x, pos.y),
				ImVec2(maxPos),
				ImVec2(0, 0),
				ImVec2(Mathf::Clamp(0.f, 1.0f, GameRenderSize.X() / Renderer->RTT->Size.X()), Mathf::Clamp(0.f, 1.0f, GameRenderSize.Y() / Renderer->RTT->Size.Y())));
				//ImVec2(WorldViewRenderSize.X() / RenderSize.X(), WorldViewRenderSize.Y() / RenderSize.Y()));
		}
	}
	ImGui::End();
	//ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	//ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	//ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("World View");
	{
		m_isWorldViewFocused = ImGui::IsWindowFocused();

		//ImGui::PopStyleVar(3);

		if (Renderer->RTT2->shaderResourceViewMap != nullptr)
		{
			// Get the current cursor position (where your window is)
			ImVec2 pos = ImGui::GetCursorScreenPos();
			ImVec2 maxPos = ImVec2(pos.x + ImGui::GetWindowSize().x, pos.y + ImGui::GetWindowSize().y);
			WorldViewRenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
			// Ask ImGui to draw it as an image:
			// Under OpenGL the ImGUI image type is GLuint
			// So make sure to use "(void *)tex" but not "&tex"
			ImGui::GetWindowDrawList()->AddImage(
				(void*)Renderer->RTT2->shaderResourceViewMap,
				ImVec2(pos.x, pos.y),
				ImVec2(maxPos),
				ImVec2(0, 0),
				ImVec2(Mathf::Clamp(0.f, 1.0f, WorldViewRenderSize.X() / Renderer->RTT2->Size.X()), Mathf::Clamp(0.f, 1.0f, WorldViewRenderSize.Y() / Renderer->RTT2->Size.Y())));
				//ImVec2(WorldViewRenderSize.X() / RenderSize.X(), WorldViewRenderSize.Y() / RenderSize.Y()));
		}
	}
	ImGui::End();

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	// Update and Render additional Platform Windows
	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
}

const bool Havana::IsGameFocused() const
{
	return m_isGameFocused;
}

const bool Havana::IsWorldViewFocused() const
{
	return m_isWorldViewFocused;
}

void Havana::Text(const std::string & Name, const Vector3 & Vector)
{
	ImGui::Text(Name.c_str());

	ImGui::Text("X: %f", Vector.X());
	ImGui::SameLine();
	ImGui::Text("Y: %f", Vector.Y());
	ImGui::SameLine();
	ImGui::Text("Z: %f", Vector.Z());
}

void Havana::EditableVector3(const std::string & Name, Vector3 & Vector)
{
	ImGui::DragFloat3(Name.c_str(), &Vector[0], 0.005f);
}

void Havana::BrowseDirectory(const FilePath & path)
{
	if (CurrentDirectory.FullPath == path.FullPath && !AssetDirectory.empty())
	{
		return;
	}

	for (const auto& entry : fs::directory_iterator(path.FullPath))
	{
		FilePath filePath(entry.path().string());

		AssetDirectory.push_back(filePath.FullPath);

		ImGui::Text(filePath.Directory.c_str());

		ImGui::Text(filePath.LocalPath.c_str());
	}
}

const Vector2& Havana::GetGameOutputSize() const
{
	return GameRenderSize;
}

#endif