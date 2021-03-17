#include "SceneHierarchyWidget.h"
#include <Engine/World.h>
#include <Engine/Engine.h>
#include <optick.h>
#include <EditorApp.h>
#include <Components/Transform.h>
#include <ECS/CoreDetail.h>
#include <Utils/CommonUtils.h>
#include <imgui.h>

SceneHierarchyWidget::SceneHierarchyWidget()
	: HavanaWidget("Scene")
{
}

void SceneHierarchyWidget::Init()
{
	App = static_cast<EditorApp*>(GetEngine().GetGame());
}

void SceneHierarchyWidget::Destroy()
{
	App = nullptr;
	Entities = nullptr;
}

void SceneHierarchyWidget::SetData(Transform* inRoot, std::vector<Entity>& inEntities)
{
	m_rootTransform = inRoot;
	Entities = &inEntities;
}

void SceneHierarchyWidget::Update()
{

}

void SceneHierarchyWidget::Render()
{
	auto world = GetEngine().GetWorld().lock();


	OPTICK_CATEGORY("Havana::UpdateWorld", Optick::Category::GameLogic);
	ImGui::Begin("World", 0, ImGuiWindowFlags_MenuBar);
	if (!world)
	{
		ImGui::End();
		return;
	}
	ImGui::BeginMenuBar();
	if (ImGui::BeginMenu("Create"))
	{
		if (ImGui::BeginMenu("Entity"))
		{
			if (ImGui::IsItemClicked())
			{
				App->Editor->SelectedEntity = GetEngine().GetWorld().lock()->CreateEntity();
			}
			if (ImGui::MenuItem("Entity Transform"))
			{
				App->Editor->SelectedEntity = GetEngine().GetWorld().lock()->CreateEntity();
				App->Editor->SelectedEntity->AddComponent<Transform>("New Entity");
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Core"))
		{
			DrawAddCoreList();
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}
	ImGui::EndMenuBar();
	if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
	{
		OPTICK_CATEGORY("Entity List", Optick::Category::GameLogic);
		if (ImGui::IsWindowFocused())
		{
			if (App->Editor->SelectedTransform && GetEngine().GetEditorInput().IsKeyDown(KeyCode::Delete))
			{
				CommonUtils::RecusiveDelete(App->Editor->SelectedTransform->Parent, App->Editor->SelectedTransform);
				App->Editor->ClearSelection();
			}
		}
		UpdateWorldRecursive(m_rootTransform);
	}
	if (Entities->size() > 0)
	{
		if (ImGui::CollapsingHeader("Utility", ImGuiTreeNodeFlags_DefaultOpen))
		{
			OPTICK_CATEGORY("Utility Entities", Optick::Category::Debug);
			int i = 0;
			for (const Entity& ent : *Entities)
			{
				for (BaseComponent* comp : ent.GetAllComponents())
				{
					ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | (App->Editor->SelectedEntity.Get() == &ent ? ImGuiTreeNodeFlags_Selected : 0);
					{
						node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
						ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, comp->GetName().c_str());
						if (ImGui::IsItemClicked())
						{
							App->Editor->SelectedCore = nullptr;
							App->Editor->SelectedTransform = nullptr;
							App->Editor->SelectedEntity = EntityHandle(ent.GetId(), world->GetSharedPtr());
						}
					}
				}
			}
		}
	}

	if (ImGui::CollapsingHeader("Entity Cores", ImGuiTreeNodeFlags_DefaultOpen))
	{
		OPTICK_CATEGORY("Entity Cores", Optick::Category::Debug);
		int i = 0;
		for (auto& comp : world->GetAllCoresArray())
		{
			ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | (App->Editor->SelectedCore == comp.second.get() ? ImGuiTreeNodeFlags_Selected : 0);
			{
				node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
				ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, comp.second->GetName().c_str());
				if (ImGui::IsItemClicked())
				{
					App->Editor->SelectedCore = comp.second.get();
					App->Editor->SelectedTransform = nullptr;
					App->Editor->SelectedEntity = EntityHandle();
				}
			}
		}
	}
	ImGui::End();

}


void SceneHierarchyWidget::DrawAddCoreList()
{
	ImGui::Text("Cores");
	ImGui::Separator();

	CoreRegistry& reg = GetCoreRegistry();

	for (auto& thing : reg)
	{
		if (ImGui::Selectable(thing.first.c_str()))
		{
			GetEngine().GetWorld().lock()->AddCoreByName(thing.first);
		}
	}
}

void SceneHierarchyWidget::UpdateWorldRecursive(Transform* root)
{
	OPTICK_CATEGORY("UpdateWorld::UpdateWorldRecursive", Optick::Category::GameLogic);
	if (!root)
		return;
	if (ImGui::BeginDragDropTarget())
	{
		App->Editor->HandleAssetDragAndDrop(root);

		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_CHILD_TRANSFORM"))
		{
			App->Editor->DragParentDescriptor.Parent->SetParent(*root);
		}
		ImGui::EndDragDropTarget();
	}

	int i = 0;
	for (SharedPtr<Transform> child : root->GetChildren())
	{
		OPTICK_CATEGORY("UpdateWorld::UpdateWorldRecursive::Child", Optick::Category::GameLogic);
		if (!child)
		{
			continue;
		}
		Transform* var = child.get();
		bool open = false;
		ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | (App->Editor->SelectedTransform == var ? ImGuiTreeNodeFlags_Selected : 0);
		if (var->GetChildren().empty())
		{
			node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
			open = ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, var->Name.c_str());
			if (ImGui::IsItemClicked())
			{
				App->Editor->SelectedTransform = var;
				App->Editor->SelectedCore = nullptr;
				App->Editor->SelectedEntity = App->Editor->SelectedTransform->Parent;
			}

			App->Editor->DrawEntityRightClickMenu(var);

			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
			{
				//files.FullPath = dir.FullPath;
				App->Editor->DragParentDescriptor.Parent = var;
				ImGui::SetDragDropPayload("DND_CHILD_TRANSFORM", &App->Editor->DragParentDescriptor, sizeof(ParentDescriptor));
				ImGui::Text(var->GetName().c_str());
				ImGui::EndDragDropSource();
			}

			if (ImGui::BeginDragDropTarget())
			{
				App->Editor->HandleAssetDragAndDrop(var);

				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_CHILD_TRANSFORM"))
				{
					App->Editor->DragParentDescriptor.Parent->SetParent(*child);
				}
				ImGui::EndDragDropTarget();
			}

			//if (open)
			//{
			//	// your tree code stuff
			//	ImGui::TreePop();
			//}
		}
		else
		{
			OPTICK_CATEGORY("UpdateWorld::UpdateWorldRecursive::Leaf", Optick::Category::GameLogic);
			open = ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, var->Name.c_str());
			if (ImGui::IsItemClicked())
			{
				App->Editor->SelectedTransform = var;
				App->Editor->SelectedCore = nullptr;
				App->Editor->SelectedEntity = App->Editor->SelectedTransform->Parent;
			}

			App->Editor->DrawEntityRightClickMenu(var);

			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
			{
				App->Editor->DragParentDescriptor.Parent = var;
				//files.FullPath = dir.FullPath;
				ImGui::SetDragDropPayload("DND_CHILD_TRANSFORM", &App->Editor->DragParentDescriptor, sizeof(ParentDescriptor));
				ImGui::Text(var->GetName().c_str());
				ImGui::EndDragDropSource();
			}

			if (ImGui::BeginDragDropTarget())
			{
				App->Editor->HandleAssetDragAndDrop(var);

				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_CHILD_TRANSFORM"))
				{
					App->Editor->DragParentDescriptor.Parent->SetParent(*child);
				}
				ImGui::EndDragDropTarget();
			}

			if (open)
			{
				UpdateWorldRecursive(var);
				ImGui::TreePop();
				//ImGui::TreePop();
			}
		}

		i++;
	}
}