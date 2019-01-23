#include "MitchGame.h"
#include "ECS/Component.h"
#include "Engine/Clock.h"
#include "Components/Transform.h"
#include "ECS/Entity.h"
#include <string>
#include "Engine/Input.h"
#include "Components/Animation.h"
#include "Components/Camera.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Graphics/Model.h"
#include "Components/Lighting/Light.h"
#include "Components/FlyingCamera.h"

#include <memory>
#include "Engine/World.h"
#include "FilePath.h"

MitchGame::MitchGame()
	: Game()
{
}

MitchGame::~MitchGame()
{
	Game::~Game();
}

void MitchGame::Initialize()
{
	auto GameWorld = GetEngine().GetWorld().lock();
	MainCamera = GameWorld->CreateEntity();
	Transform& CameraPos = MainCamera.AddComponent<Transform>("Main Camera");
	CameraPos.SetPosition(glm::vec3(0, 5, 20));
	MainCamera.AddComponent<Camera>();
	MainCamera.AddComponent<FlyingCamera>();
	MainCamera.AddComponent<Light>();

	SecondaryCamera = GameWorld->CreateEntity();
	Transform& SecondaryPos = SecondaryCamera.AddComponent<Transform>("Secondary Camera");
	SecondaryPos.SetPosition(glm::vec3(0, 5, 20));
	SecondaryCamera.AddComponent<Camera>();
	SecondaryCamera.AddComponent<Light>();
	SecondaryCamera.AddComponent<FlyingCamera>();

	Entity TestModel = GameWorld->CreateEntity();
	Transform& ModelTransform = TestModel.AddComponent<Transform>("Ground obvs");
	ModelTransform.SetPosition(glm::vec3(0.f, 20.f, 0.f));
	ModelTransform.SetScale(glm::vec3(0.5f, 0.5f, 0.5f));
	TestModel.AddComponent<Rigidbody>();
	TestModel.AddComponent<Model>("Assets/Mech/mech.fbx", "Assets/Shaders/Albedo");


	Entity TestModel2 = GameWorld->CreateEntity();
	Transform& ModelTransform2 = TestModel2.AddComponent<Transform>("Ground obvs2");
	ModelTransform2.SetPosition(glm::vec3(5.f, 20.f, 0.f));
	TestModel2.AddComponent<Rigidbody>();
	TestModel2.AddComponent<Model>("Assets/Mech/mech.fbx", "Assets/Shaders/Albedo");

	/*Entity Ground2 = GameWorld->CreateEntity();
	Ground2.AddComponent<Transform>("Ground obvs");
	Ground2.GetComponent<Transform>().SetPosition(glm::vec3(0, 0, 0));
	Ground2.GetComponent<Transform>().SetScale(glm::vec3(0.025f, 0.025f, 0.025f));
	Ground2.AddComponent<Model>("Assets/Hog/Roadhog.fbx", "Assets/Shaders/Albedo");*/

	const int Lights = 5;
	srand(13);
	for (unsigned int i = 0; i < Lights; i++)
	{
		Entity TestLight = GameWorld->CreateEntity();
		Transform& LightTransform = TestLight.AddComponent<Transform>("Light " + std::to_string(i));
		Light& LightInfo = TestLight.AddComponent<Light>();

		float xPos = ((rand() % 100) / 100.0) * 6.0 - 3.0;
		float yPos = ((rand() % 100) / 100.0) * 6.0 - 4.0;
		float zPos = ((rand() % 100) / 100.0) * 6.0 - 3.0;
		LightTransform.SetPosition(glm::vec3(xPos, yPos, zPos));
	}

	FlyingCameraController = new FlyingCameraCore();
	GameWorld->AddCore<FlyingCameraCore>(*FlyingCameraController);
}

float totalTime = 0.f;
void MitchGame::Update(float DeltaTime)
{
	FlyingCameraController->Update(DeltaTime);
	Input& Instance = Input::GetInstance();
	if (Instance.IsKeyDown(KeyCode::Number1))
	{
		MainCamera.GetComponent<Camera>().SetCurrent();
	}
	if (Instance.IsKeyDown(KeyCode::Number2))
	{
		SecondaryCamera.GetComponent<Camera>().SetCurrent();
	}
	if (Instance.IsKeyDown(KeyCode::P) && !AddedPhysics)
	{
		for (auto& Cube : Cubes)
		{
			Cube.AddComponent<Rigidbody>();
		}
		AddedPhysics = true;
	}
	int i = 0;
	for (auto& Cube : Cubes)
	{
		Transform& CubeTransform = Cube.GetComponent<Transform>();
		CubeTransform.SetPosition(glm::vec3(i, glm::sin(totalTime + i), 0));
		i++;
	}

	totalTime += DeltaTime;

	Camera* CurrentCamera = Camera::CurrentCamera;
	{
		if (CurrentCamera->Zoom >= 1.0f && CurrentCamera->Zoom <= 45.0f)
			CurrentCamera->Zoom -= PrevMouseScroll.y - Input::GetInstance().GetMouseScrollOffset().y;
		if (CurrentCamera->Zoom <= 1.0f)
			CurrentCamera->Zoom = 1.0f;
		if (CurrentCamera->Zoom >= 45.0f)
			CurrentCamera->Zoom = 45.0f;
	}
	PrevMouseScroll = Input::GetInstance().GetMouseScrollOffset();
	Transform& TransformComponent = MainCamera.GetComponent<Transform>();
}

void MitchGame::End()
{
}