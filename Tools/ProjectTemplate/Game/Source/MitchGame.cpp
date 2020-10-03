#include "MitchGame.h"
#include "Engine/Engine.h"
#include "ECS/Component.h"
#include "Engine/Clock.h"
#include "Components/Transform.h"
#include "ECS/Entity.h"
#include <string>
#include "Engine/Input.h"
#include "Components/Camera.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Graphics/Model.h"
#include "Components/Lighting/Light.h"
#include "Components/Cameras/FlyingCamera.h"
#include "Cores/TestCore.h"
#include <memory>
#include "Engine/World.h"
#include "Path.h"
#include "Game.h"
#include "Cores/PhysicsCore.h"
#include "Cores/Cameras/FlyingCameraCore.h"
#include "Events/SceneEvents.h"

MitchGame::MitchGame()
	: Game()
{
	FlyingCameraController = new FlyingCameraCore();
}

MitchGame::~MitchGame()
{
}

void MitchGame::OnStart()
{
	return;
	GetEngine().LoadScene("Assets/Main.lvl");
	auto GameWorld = GetEngine().GetWorld().lock();

	MainCamera = GameWorld->CreateEntity();
	Transform& CameraPos = MainCamera->AddComponent<Transform>("Main Camera");
	CameraPos.SetPosition(Vector3(0, 5, 20));
	Camera& cam = MainCamera->AddComponent<Camera>();
	MainCamera->AddComponent<FlyingCamera>();
	MainCamera->AddComponent<Light>();

	SecondaryCamera = GameWorld->CreateEntity();
	Transform& SecondaryPos = SecondaryCamera->AddComponent<Transform>("Secondary Camera");
	SecondaryPos.SetPosition(Vector3(0, 5, 20));
	SecondaryCamera->AddComponent<Camera>();
	SecondaryCamera->AddComponent<Light>();
	SecondaryCamera->AddComponent<FlyingCamera>();

	auto TestModel = GameWorld->CreateEntity();
	Transform& ModelTransform = TestModel->AddComponent<Transform>("Sponza");
	ModelTransform.SetPosition(Vector3(0.f, 0.f, 0.f));
	ModelTransform.SetScale(Vector3(.1f, .1f, .1f));
	//TestModel.lock()->AddComponent<Rigidbody>();
	//TestModel.lock()->AddComponent<Model>("Assets/ExampleAssets/Models/Hammer.fbx");
	TestModel->AddComponent<Model>("Assets/Craftsman/Craftsman.fbx");

	FlyingCameraController->SetCamera(&cam);
	GameWorld->AddCore<FlyingCameraCore>(*FlyingCameraController);
}

void MitchGame::OnUpdate(float DeltaTime)
{
	FlyingCameraController->Update(DeltaTime);

	Input& Instance = GetEngine().GetInput();
	if (Instance.GetKeyboardState().NumPad1)
	{
		MainCamera->GetComponent<Camera>().SetCurrent();
	}
	if (Instance.GetKeyboardState().NumPad2)
	{
		SecondaryCamera->GetComponent<Camera>().SetCurrent();
	}
}

void MitchGame::OnEnd()
{
}

void MitchGame::OnInitialize()
{
	NewSceneEvent evt;
	evt.Fire();
	GetEngine().GetWorld().lock()->Start();
	GetEngine().LoadScene("Assets/Scenes/SceneGraphTest.lvl");
	GetEngine().GetWorld().lock()->Simulate();
	GetEngine().GetWorld().lock()->Start();
}

void MitchGame::PostRender()
{
}