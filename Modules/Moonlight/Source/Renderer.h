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
#include "Camera/CameraData.h"
#include "GeometricPrimitive.h"
#include <PrimitiveBatch.h>
#include "Content/ShaderStructures.h"
#include <SpriteFont.h>
#include <SpriteBatch.h>

namespace Moonlight
{
	class Renderer
	{
	public:

		void UpdateMatrix(unsigned int Id, DirectX::SimpleMath::Matrix NewTransform);
		void UpdateMeshMatrix(unsigned int Id, DirectX::SimpleMath::Matrix NewTransform);
		void UpdateText(unsigned int Id, TextCommand NewCommand);
	public:
		Renderer();
		virtual ~Renderer() final;

		void Init();

		void SetWindow();
		void RegisterDeviceNotify(IDeviceNotify* deviceNotify);

		D3D12Device& GetDevice();

		void Update(float dt);
		void Render(std::function<void()> func, const CameraData& mainCamera, const CameraData& editorCamera);

		void DrawScene(ID3D11DeviceContext3* context, ModelViewProjectionConstantBuffer& constantBufferSceneData, const CameraData& data, FrameBuffer* ViewRTT, FrameBuffer* ResolveViewRTT);

		void ReleaseDeviceDependentResources();
		void CreateDeviceDependentResources();
		void InitD2DScreenTexture();

		unsigned int PushDebugCollider(const DebugColliderCommand& model);
		bool PopDebugCollider(unsigned int id);

		void ClearDebugColliders();

		unsigned int PushLight(const LightCommand& NewLight);
		bool PopLight(unsigned int id);

		void ClearLights();

		void WindowResized(const Vector2& NewSize);

		//class RenderTexture* GameViewRTT = nullptr;
		FrameBuffer* SceneViewRTT = nullptr;
		FrameBuffer* SceneResolveViewRTT = nullptr;
		FrameBuffer* m_framebuffer = nullptr;
		FrameBuffer* m_resolvebuffer = nullptr;
		LightCommand Sunlight;

		ShaderProgram m_tonemapProgram;
		ShaderProgram m_dofProgram;
		ShaderCommand m_depthProgram;
		ShaderProgram m_lightingProgram;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> m_defaultSampler;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> m_computeSampler;
	private:
		class Plane* Grid;

		class D3D12Device* m_device = nullptr;

		void ResizeBuffers();
#if ME_DIRECTX
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_perFrameBuffer;
		ModelViewProjectionConstantBuffer m_constantBufferData;
		ModelViewProjectionConstantBuffer m_constantBufferSceneData;
		std::unique_ptr<DirectX::PrimitiveBatch<VertexPositionTexCoord>> primitiveBatch;
#endif
		// Text
		std::unique_ptr<DirectX::SpriteFont> m_font;
		DirectX::SimpleMath::Vector2 m_fontPos;
		std::unique_ptr<DirectX::SpriteBatch> m_spriteBatch;

		// Commands
		std::vector<DebugColliderCommand> DebugColliders;
		std::queue<unsigned int> FreeDebugColliderCommandIndicies;

		std::vector<LightCommand> Lights;
		std::queue<unsigned int> FreeLightCommandIndicies;

		std::vector<MeshCommand> Meshes;
		std::queue<unsigned int> FreeMeshCommandIndicies;

		std::vector<TextCommand> UIText;
		std::queue<unsigned int> FreeUITextCommandIndicies;

#if ME_ENABLE_RENDERDOC
		RenderDocManager* RenderDoc;
#endif
	public:
		unsigned int PushMesh(Moonlight::MeshCommand command);
		void PopMesh(unsigned int Id);
		void ClearMeshes();

		unsigned int PushUIText(Moonlight::TextCommand command);
		void PopUIText(unsigned int Id);
		void ClearUIText();

		std::unique_ptr<DirectX::GeometricPrimitive> shape;
	};
}