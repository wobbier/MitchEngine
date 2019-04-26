#pragma once
#include <memory>

#include "Device/IDevice.h"
#include "Singleton.h"
#include "Resource/ResourceCache.h"
#include "Graphics/ModelResource.h"
#include "Utils/DirectXHelper.h"

#if ME_ENABLE_RENDERDOC
#include "Debug/RenderDocManager.h"
#endif
#include <d3d11.h>
#include <DirectXMath.h>
#include <queue>
#include "Math/Vector2.h"
#include <functional>
#include "RenderCommands.h"

namespace Moonlight
{
	class Renderer
	{
	public:

		void UpdateMatrix(unsigned int Id, DirectX::XMMATRIX NewTransform);
	public:
		Renderer();
		virtual ~Renderer() final;

		void Init();
		void SetWindow();
		void RegisterDeviceNotify(IDeviceNotify* deviceNotify);

		D3D12Device& GetDevice();

		void Update(float dt);
		void Render(std::function<void()> func, const CameraData& mainCamera, const CameraData& editorCamera);

		void DrawScene(ID3D11DeviceContext3* context, ModelViewProjectionConstantBuffer& constantBufferSceneData, const CameraData& data);

		void ReleaseDeviceDependentResources();
		void CreateDeviceDependentResources();
		void InitD2DScreenTexture();

		unsigned int PushModel(const ModelCommand& model);
		bool PopModel(unsigned int id);

		void ClearModels();

		unsigned int PushLight(const LightCommand& NewLight);
		bool PopLight(unsigned int id);

		void ClearLights();

		void WindowResized(const Vector2& NewSize);

		class RenderTexture* RTT = nullptr;
		class RenderTexture* RTT2 = nullptr;

	private:
		class SkyBox* Sky;
		class Plane* Grid;

		class D3D12Device* m_device;

#if ME_DIRECTX
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_perFrameBuffer;
		ModelViewProjectionConstantBuffer m_constantBufferData;
		ModelViewProjectionConstantBuffer m_constantBufferSceneData;
		LightCommand light;

		LightBuffer m_perFrameBufferData;

		float m_degreesPerSecond = 45.f;
#endif

		std::vector<ModelCommand> Models;
		std::queue<unsigned int> FreeCommandIndicies;

		std::vector<LightCommand> Lights;
		std::queue<unsigned int> FreeLightCommandIndicies;
#if ME_ENABLE_RENDERDOC
		RenderDocManager* RenderDoc;
#endif
	};
}