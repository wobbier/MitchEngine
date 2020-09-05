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
#include "Graphics/ShaderStructures.h"
#include <SpriteFont.h>
#include <SpriteBatch.h>
#include "Events/Event.h"

namespace Moonlight
{
	class PickingEvent
		: public Event<PickingEvent>
	{
	public:
		int Id = 0;
	};

	enum class eDeviceContextType : int8_t
	{
		Immediate = 0,
		ST_DefferedScene,
		MT_DefferedScene,
		ST_DefferedChunk,
		MT_DefferedChunk,
		Legacy
	};

	// The different types of job in the per-chunk work queues
	enum WorkQueueEntryType
	{
		WORK_QUEUE_ENTRY_TYPE_SETUP,
		WORK_QUEUE_ENTRY_TYPE_CHUNK,
		WORK_QUEUE_ENTRY_TYPE_FINALIZE,

		WORK_QUEUE_ENTRY_TYPE_COUNT
	};

	// The contents of the work queues depend on the job type...
	struct WorkQueueEntryBase
	{
		WorkQueueEntryType          m_iType;
	};

	// Work item params for scene setup
	struct WorkQueueEntrySetup : public WorkQueueEntryBase
	{
		//const SceneParamsStatic* m_pSceneParamsStatic;
		//SceneParamsDynamic          m_SceneParamsDynamic;
		FrameBuffer* buffer;
		CameraData* m_camera;
		ID3D11Buffer* m_constantBufferBuffer;
	};

	// Work item params for chunk render
	struct WorkQueueEntryChunk : public WorkQueueEntryBase
	{
		int                         m_iMesh;
		std::function<void(ID3D11DeviceContext3*)> m_renderMesh;
	};

	// Work item params for scene finalize
	struct WorkQueueEntryFinalize : public WorkQueueEntryBase
	{
	};

	// The work item queue for each per-chunk worker thread
	const int                   g_iSceneQueueSizeInBytes = 16 * 1024;
	typedef BYTE                ChunkQueue[g_iSceneQueueSizeInBytes];

	class Renderer
	{
	public:
		enum class RenderPassType
		{
			Differed,
			Forward
		};
		RenderPassType PassType = RenderPassType::Differed;
		void UpdateMatrix(unsigned int Id, DirectX::SimpleMath::Matrix NewTransform);
		void UpdateMeshMatrix(unsigned int Id, DirectX::SimpleMath::Matrix& NewTransform);
		void UpdateCamera(unsigned int Id, CameraData& NewCommand);
		Moonlight::CameraData& GetCamera(unsigned int Id);
	public:
		Renderer();
		virtual ~Renderer() final;

		void Init();

		void SetWindow();
		void RegisterDeviceNotify(IDeviceNotify* deviceNotify);

		void ClearBuffer(ID3D11DeviceContext3* context, FrameBuffer* buffer, CameraData* camera);

/* MULTITHREADING */
		int GetPhysicalProcessorCount();
		void InitializeWorkerThreads(ID3D11Device* device);
		static unsigned int _PerChunkRenderDeferredProc(LPVOID lpParameter);

		void ThreadedRender(std::function<void()> func, std::function<void()> uiRender, CameraData& editorCamera);
		void RenderSceneDirect(ID3D11DeviceContext3* context, const ModelViewProjectionConstantBuffer& constantBufferSceneData, CameraData& camera, FrameBuffer* frameBuffer);

		void RenderMeshDirect(MeshCommand& mesh, ID3D11DeviceContext3* context);

		static void RenderSceneSetup(Renderer& renderer, ID3D11DeviceContext3* context, const CameraData& camera, FrameBuffer* ViewRTT, ID3D11Buffer* constantBuffer);
		void FinishRenderingScene(ID3D11DeviceContext3* context, const CameraData& camera, FrameBuffer* ViewRTT, ID3D11Buffer* constantBuffer);

		void SetContextType(eDeviceContextType inType);
		eDeviceContextType GetContextType();
		// Device Context Type
		inline bool IsRenderDeferredPerScene() const
		{
			return m_contextType == eDeviceContextType::ST_DefferedScene || m_contextType == eDeviceContextType::MT_DefferedScene;
		}

		inline bool IsRenderMultithreadedPerScene() const
		{
			return m_contextType == eDeviceContextType::MT_DefferedScene;
		}

		inline bool IsRenderDeferredPerChunk() const
		{
			return m_contextType == eDeviceContextType::ST_DefferedChunk || m_contextType == eDeviceContextType::MT_DefferedChunk;
		}

		inline bool IsRenderMultithreadedPerChunk() const
		{
			return m_contextType == eDeviceContextType::MT_DefferedChunk;
		}

		bool IsLegacyRenderer()
		{
			return m_contextType == eDeviceContextType::Legacy;
		}

		// Device Context Type
/* MULTITHREADING */

		DX11Device& GetDevice() const;

		void Update(float dt);
		void Render(std::function<void()> func, std::function<void()> uiRender, const CameraData& editorCamera);

		void DrawScene(ID3D11DeviceContext3* context, ModelViewProjectionConstantBuffer& constantBufferSceneData, const CameraData& data, FrameBuffer* ViewRTT);
		void DrawDepthOnlyScene(ID3D11DeviceContext3* context, DepthPassBuffer& constantBufferSceneData, FrameBuffer* ViewRTT);
		void DrawPickingTexture(ID3D11DeviceContext3* context, PickingConstantBuffer& constantBufferSceneData, const CameraData& camera, FrameBuffer* ViewRTT);

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

		void PickScene(const Vector2& Pos);

		//class RenderTexture* GameViewRTT = nullptr;
		FrameBuffer* SceneViewRTT = nullptr;
		FrameBuffer* GameViewRTT = nullptr;
		LightCommand Sunlight;
		LightingPassConstantBuffer LightingPassBuffer;

		ShaderProgram m_tonemapProgram;
		ShaderProgram m_dofProgram;
		ShaderProgram m_depthProgram;
		ShaderProgram m_pickingShader;
		ShaderProgram m_lightingProgram;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> m_defaultSampler;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> m_computeSampler;

	private:
/* MULTITHREADING */
		eDeviceContextType m_contextType = eDeviceContextType::MT_DefferedChunk;
		int m_numPerChunkRenderThreads = 0;
		static const int kMaxPerChunkRenderThreads = 32;
		static const int m_maxPendingQueueEntries = 1024;     // Max value of g_hBeginPerChunkRenderDeferredSemaphore
		int m_perChunkThreadInstanceData[kMaxPerChunkRenderThreads];
		int g_iPerChunkQueueOffset[kMaxPerChunkRenderThreads]; // next free portion of the queue to add an entry to
		HANDLE m_hPerChunkRenderDeferredThread[kMaxPerChunkRenderThreads];
		HANDLE m_hBeginPerChunkRenderDeferredSemaphore[kMaxPerChunkRenderThreads];
		HANDLE m_hEndPerChunkRenderDeferredEvent[kMaxPerChunkRenderThreads];
		ID3D11DeviceContext3* m_pd3dPerChunkDeferredContext[kMaxPerChunkRenderThreads] = { nullptr };
		ID3D11CommandList* g_pd3dPerChunkCommandList[kMaxPerChunkRenderThreads] = { nullptr };
		ChunkQueue m_chunkQueue[kMaxPerChunkRenderThreads];
/* MULTITHREADING */

		class MeshData* PlaneMesh = nullptr;

		class DX11Device* m_device = nullptr;
		std::vector<Vertex> m_planeVerticies;
		std::vector<uint16_t> m_planeIndicies;
		void ResizeBuffers();
		void SaveTextureToBmp(PCWSTR FileName, ID3D11Texture2D* Texture, const CameraData& camera, const Vector2& Pos);
#if ME_DIRECTX
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_perFrameBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_lightingBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_depthPassBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_pickingBuffer;
		ModelViewProjectionConstantBuffer m_constantBufferData;
		ModelViewProjectionConstantBuffer m_constantBufferSceneData;
		PerFrameConstantBuffer m_perFrameConstantBuffer;
		std::unique_ptr<DirectX::PrimitiveBatch<VertexPositionTexCoord>> primitiveBatch;
#endif
		bool m_pickingRequested = false;
		Vector2 pickingLocation;

		// Commands
		std::vector<DebugColliderCommand> DebugColliders;
		std::queue<unsigned int> FreeDebugColliderCommandIndicies;

		std::vector<LightCommand> Lights;
		std::queue<unsigned int> FreeLightCommandIndicies;

		std::vector<MeshCommand> Meshes;
		std::queue<unsigned int> FreeMeshCommandIndicies;

		std::vector<CameraData> Cameras;
		std::queue<unsigned int> FreeCameraCommandIndicies;

#if ME_ENABLE_RENDERDOC
		RenderDocManager* RenderDoc;
#endif
		void RenderMeshes();
		void RenderMesh(int i, ID3D11DeviceContext3* context);
	public:
		unsigned int PushMesh(Moonlight::MeshCommand command);
		void PopMesh(unsigned int Id);
		void ClearMeshes();

		unsigned int PushCamera(Moonlight::CameraData& command);
		void PopCamera(unsigned int Id);
		void ClearUIText();

		std::unique_ptr<DirectX::GeometricPrimitive> shape;
	};
}