#include "SceneGraphTestCore.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Mathf.h"
#include "optick.h"
#include "Work/Burst.h"

SceneGraphTestObject::SceneGraphTestObject()
	: Component("SceneGraphTestObject")
{
}

void SceneGraphTestObject::Init()
{
}

void SceneGraphTestObject::OnSerialize(json& outJson)
{
}

void SceneGraphTestObject::OnDeserialize(const json& inJson)
{
}

SceneGraphTestCore::SceneGraphTestCore()
	: Base(ComponentFilter().Requires<Transform>().Requires<SceneGraphTestObject>())
{

}

void SceneGraphTestCore::OnEntityAdded(Entity& NewEntity)
{
}

void SceneGraphTestCore::OnEntityRemoved(Entity& InEntity)
{
}

#if ME_EDITOR
void SceneGraphTestCore::OnEditorInspect()
{
	Base::OnEditorInspect();
}
#endif

void SceneGraphTestCore::Update(float dt)
{
	OPTICK_CATEGORY("SceneGraphTestCore::Update", Optick::Category::GameLogic);

	auto& Entities = GetEntities();

	Burst& burst = GetEngine().GetBurstWorker();
	burst.PrepareWork();

	std::vector<std::pair<int, int>> batches;
	burst.GenerateChunks(Entities.size(), 11, batches);

	for (auto& batch : batches)
	{
		OPTICK_CATEGORY("Burst::BatchAdd", Optick::Category::Debug);
		Burst::LambdaWorkEntry entry;
		int batchBegin = batch.first;
		int batchEnd = batch.second;
		int batchSize = batchEnd - batchBegin;

		entry.m_callBack = [this, &Entities, batchBegin, batchEnd, dt](int Index) {
			OPTICK_CATEGORY("B::Job", Optick::Category::Debug);

			for (int entIndex = batchBegin; entIndex < batchEnd; ++entIndex)
			{
				auto& InEntity = Entities[entIndex];
				OPTICK_CATEGORY("Update Rotation", Optick::Category::Debug);
				Transform& transform = InEntity.GetComponent<Transform>();
				{
					OPTICK_CATEGORY("Update Rotation Vector", Optick::Category::Debug);
					Vector3 rot = transform.GetRotation();
					rot.SetY(rot.Y() + (Mathf::Radians(10.0f * dt * 50.f)));
					transform.SetRotation(rot);
				}
			}
		};

		burst.AddWork2(entry, (int)sizeof(Burst::LambdaWorkEntry));
	}

	burst.FinalizeWork();
}

void SceneGraphTestCore::OnEntityDestroyed(Entity& InEntity)
{
}

void SceneGraphTestCore::Init()
{
}

void SceneGraphTestCore::OnStart()
{
	auto world = GetEngine().GetWorld().lock();

	EntityHandle rootEnt = world->CreateFromPrefab(std::string(kPrefabName));
	Transform& rootTransform = rootEnt->GetComponent<Transform>();

	for (int i = 0; i < 10; ++i)
	{
		EntityHandle subEnt = world->CreateFromPrefab(std::string(kPrefabName2), &rootTransform);
		Transform& transform = subEnt->GetComponent<Transform>();
		transform.SetPosition(Vector3(((float)i - 4.5f) * 2.f, -3.f, 0.f));
		transform.SetScale(0.5f);

		for (int j = 0; j < 10; ++j)
		{
			EntityHandle sub2Ent = world->CreateFromPrefab(std::string(kPrefabName3), &transform);
			Transform& subTransform = sub2Ent->GetComponent<Transform>();
			subTransform.SetPosition(Vector3(((float)j - 4.5f) * 2.f, -3.f, 0.f));
			subTransform.SetScale(0.5f);

			for (int k = 0; k < 10; ++k)
			{
				EntityHandle sub3Ent = world->CreateFromPrefab(std::string(kPrefabName4), &subTransform);
				Transform& sub3Transform = sub3Ent->GetComponent<Transform>();
				sub3Transform.SetPosition(Vector3(((float)k - 4.5f) * 2.f, -3.f, 0.f));
				sub3Transform.SetScale(0.5f);
			}
		}
	}
}

void SceneGraphTestCore::OnStop()
{
}
