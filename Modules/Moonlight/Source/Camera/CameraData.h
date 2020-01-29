#pragma once
#include "Math/Vector3.h"
#include "Math/Vector2.h"

namespace Moonlight { class SkyBox; }
namespace Moonlight { class FrameBuffer; }

namespace Moonlight
{
	enum class ProjectionType : uint8_t
	{
		Perspective = 0,
		Orthographic
	};

	enum class ClearColorType : uint8_t
	{
		Color = 0,
		Skybox
	};

	struct CameraData
	{
		Vector3 Position;
		Vector3 Front;
		Vector3 Up;
		Vector3 ClearColor;
		ClearColorType ClearType = ClearColorType::Color;
		Vector2 OutputSize;
		ProjectionType Projection = ProjectionType::Perspective;
		float FOV = 45.0f;
		SkyBox* Skybox = nullptr;
		float OrthographicSize = 1.f;

		Moonlight::FrameBuffer* Buffer = nullptr;
		bool IsMain = false;
	};
}