#include "Havana.h"
#include "imgui.h"
#include "examples/imgui_impl_win32.h"
#include "examples/imgui_impl_dx11.h"
#include "Engine/World.h"
#include "Components/Transform.h"
#include <stack>
#include "Components/Camera.h"

Havana::Havana(Moonlight::Renderer* renderer)
	: Renderer(renderer)
{
	InitUI();
}

void Havana::InitUI()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init(Renderer->GetDevice().m_window);
	ImGui_ImplDX11_Init(Renderer->GetDevice().GetD3DDevice(), Renderer->GetDevice().GetD3DDeviceContext());
}

bool show_demo_window = true;
void Havana::NewFrame()
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	static float f = 0.0f;

	ImGui::Begin("Debug Info");

	ImGui::Checkbox("Demo Window", &show_demo_window);
	ImGui::ShowDemoWindow(&show_demo_window);

	ImGui::Text("FPS Average: %.3f FPS: (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	ImGui::End();
}

void Havana::UpdateWorld(World* world, Transform* root)
{
	ImGui::Begin("Scene View");
	UpdateWorldRecursive(root);
	ImGui::End();

	if (SelectedTransform != nullptr)
	{
		ImGui::Begin("Properties");
		Entity* entity = world->GetEntity(SelectedTransform->Parent);
		if (entity)
		{
			for (BaseComponent* comp : entity->GetAllComponents())
			{
				comp->OnEditorInspect();
			}
		}

		ImGui::End();
	}
}

void Havana::UpdateWorldRecursive(Transform* root)
{
	static int selection_mask = (1 << 2);
	int i = 0;
	for (Transform* var : root->Children)
	{
		ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ((selection_mask & (1 << i)) ? ImGuiTreeNodeFlags_Selected : 0);
		if (var->Children.empty())
		{
			node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
			ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, var->Name.c_str());
			if (ImGui::IsItemClicked())
			{
				SelectedTransform = var;
			}
			continue;
		}

		bool node_open = ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, var->Name.c_str());
		if (ImGui::IsItemClicked())
		{
			SelectedTransform = var;
		}

		if (node_open)
		{
			UpdateWorldRecursive(var);
			ImGui::TreePop();
		}
		i++;
	}
}

void Havana::Render()
{
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

