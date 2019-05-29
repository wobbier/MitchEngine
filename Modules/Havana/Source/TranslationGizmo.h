#pragma once
#include "Pointers.h"
#include "ECS/Entity.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include <DirectXMath.h>
#include "gtc/matrix_transform.hpp"
#include "Engine/Input.h"
#include "Havana.h"
#include "Utils/HavanaUtils.h"

class TranslationGizmo {
public:
	enum class GizmoMode : unsigned int
	{
		None = 0,
		Translate,
		Rotate,
		Scale
	};

	struct Ray
	{
		glm::vec3 Origin;
		glm::vec3 Direction;
		float t = 0.f;
	};

	class Havana* m_editor = nullptr;

	TranslationGizmo(Havana* editor)
		: m_editor(editor)
	{
		XAxis.Direction = glm::vec3(1, 0, 0);
	}

	const bool IsActive() const {
		return false;
	}

	void Update(Transform* entity, Camera* cam)
	{
		if (!entity)
		{
			return;
		}

		const Vector2& windowLocation = m_editor->WorldViewRenderLocation;
		glm::vec2 pos = glm::vec2(Input::GetInstance().GetMousePosition().X() - windowLocation.X(), Input::GetInstance().GetMousePosition().Y() - windowLocation.Y());
		Vector2 outputSize = m_editor->WorldViewRenderSize;
		glm::vec2 n(pos.x / outputSize.X(), pos.y / outputSize.Y());
		glm::vec2 n2 = (2.f * n) - 1.f;


		ImGui::Begin("Window Info");

		HavanaUtils::Text("Window Location", windowLocation);
		HavanaUtils::Text("Relative Mouse Position", Vector2(pos));
		HavanaUtils::Text("Normalized Mouse Coordinates", Vector2(n2));

		//// gives mouse pixel coordinates in the [-1, 1] range
		//DirectX::XMFLOAT2 n(0,0);
		//float aspectRatio = static_cast<float>(outputSize.X()) / static_cast<float>(outputSize.Y());
		//float fovAngleY = cam->GetFOV() * DirectX::XM_PI / 180.0f;

		//// This is a simple example of change that can be made when the app is in
		//// portrait or snapped view.
		//if (aspectRatio < 1.0f)
		//{
		//	fovAngleY *= 2.0f;
		//}

		//// Note that the OrientationTransform3D matrix is post-multiplied here
		//// in order to correctly orient the scene to match the display orientation.
		//// This post-multiplication step is required for any draw calls that are
		//// made to the swap chain render target. For draw calls to other targets,
		//// this transform should not be applied.

		//// This sample makes use of a right-handed coordinate system using row-major matrices.
		//DirectX::XMMATRIX WorldTransform = DirectX::XMMatrixPerspectiveFovRH(
		//	fovAngleY,
		//	aspectRatio,
		//	1.f,
		//	100.0f
		//);

		glm::mat4 projection = glm::perspective(glm::radians(cam->GetFOV()), outputSize.X() / outputSize.Y(), 1.0f, 100.0f);

/*
		DirectX::XMMATRIX mat = DirectX::XMMATRIX(
			WorldTransform[0][0], WorldTransform[0][1], WorldTransform[0][2], WorldTransform[0][3],
			WorldTransform[1][0], WorldTransform[1][1], WorldTransform[1][2], WorldTransform[1][3],
			WorldTransform[2][0], WorldTransform[2][1], WorldTransform[2][2], WorldTransform[2][3],
			WorldTransform[3][0], WorldTransform[3][1], WorldTransform[3][2], WorldTransform[3][3]);*/
		//DirectX::XMMatrixIdentity();


		glm::vec4 ray_start, ray_end;
		glm::mat4 view_proj_inverse = glm::inverse(projection);
		glm::vec4 vec = { n2.x, n2.y, 0.f, 1.f };
		ray_start = view_proj_inverse * vec;
		ray_start *= 1.f / ray_start.w;

		ray_end = view_proj_inverse * glm::vec4(n2.x, n2.y, 1.f, 1.f);
		ray_end *= 1.f / ray_end.w;

		CameraRay.Origin = ray_start;
		CameraRay.Direction = glm::normalize(ray_end - ray_start);

		CameraRay.t = FLT_MAX;

		XAxis.Origin = entity->GetWorldPosition().GetInternalVec();
		XAxis.Direction = glm::normalize(XAxis.Origin + glm::vec3(1, 0, 0));

		float close = ClosestDistanceBetweenTwoLines(XAxis, CameraRay);
		if (close < 4.f)
		{
			std::cout << "Close" << std::endl;
		}
		ImGui::Text("Close X: %f", close);
		ImGui::End();
	}

	float ClosestDistanceBetweenTwoLines(Ray& l1, Ray& l2)
	{
		const glm::vec3 dp = l2.Origin - l1.Origin;
		const float v12 = dot(l1.Direction, l1.Direction);
		const float v22 = dot(l2.Direction, l2.Direction);
		const float v1v2 = dot(l1.Direction, l2.Direction);

		const float det = v1v2 * v1v2 - v12 * v22;

		if (std::abs(det) > FLT_MIN)
		{
			const float inv_det = 1.f / det;

			const float dpv1 = dot(dp, l1.Direction);
			const float dpv2 = dot(dp, l2.Direction);

			l1.t = inv_det * (v22 * dpv1 - v1v2 * dpv2);
			l2.t = inv_det * (v1v2 * dpv1 - v12 * dpv2);

			return Vector3(dp + l2.Direction * l2.t - l1.Direction * l1.t).Length();
		}
		else
		{
			const glm::vec3 a = cross(dp, l1.Direction);
			return std::sqrt(dot(a, a) / v12);
		}
	}

	Ray CameraRay;
	Ray XAxis;
};