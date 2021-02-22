#pragma once
#include "RenderCommands.h"

namespace Moonlight
{
	struct PerFrameConstantBuffer
	{
		float test;
		//DirectX::XMFLOAT4X4 view;
		//DirectX::XMFLOAT4X4 projection;
		//DirectX::XMFLOAT2 ViewportSize;
		//DirectX::XMFLOAT2 _Padding;
	};
	// Constant buffer used to send MVP matrices to the vertex shader.
	struct ModelViewProjectionConstantBuffer
	{
		float test;
		//DirectX::XMFLOAT4X4 model;
		//DirectX::XMFLOAT4X4 modelInv;
		//DirectX::XMFLOAT2 Tiling;
		//BOOL HasNormalMap;
		//BOOL HasAlphaMap;
		//BOOL HasSpecMap;
		//DirectX::XMFLOAT3 DiffuseColor;
		//DirectX::XMFLOAT4 padding2;
	};

	struct PickingConstantBuffer
	{
		float test;
		//DirectX::XMFLOAT4X4 model;
		//DirectX::XMFLOAT4X4 view;
		//DirectX::XMFLOAT4X4 projection;
		//FLOAT id;
		//DirectX::XMFLOAT3 padding;
	};

	struct DepthPassBuffer
	{
		float test;
		//DirectX::XMFLOAT4X4 model;
		//DirectX::XMFLOAT4X4 cameraMatrix;
	};

	struct LightingPassConstantBuffer
	{
		LightCommand Light;
		//DirectX::XMMATRIX LightSpaceMatrix;
	};

	// Used to send per-vertex data to the vertex shader.
	struct VertexPositionColor
	{
		float test;
		/*DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 color;*/
	};
	struct VertexPositionTexCoord
	{
		float test;
		/*DirectX::XMFLOAT3 Position;
		DirectX::XMFLOAT2 TexCoord;*/
	};
	struct Vertex
	{
		Vector3 Position;
		Vector3 Normal;
		Vector2 TextureCoord;
		Vector3 Tangent;
		Vector3 BiTangent;
	};

	struct ShaderProgram
	{
		float test;
		/*Microsoft::WRL::ComPtr<ID3D11VertexShader> VertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> PixelShader;
		Microsoft::WRL::ComPtr<ID3D11InputLayout> InputLayout;*/
	};

	struct ComputeProgram
	{
		float test;
		//Microsoft::WRL::ComPtr<ID3D11ComputeShader> ComputeShader;
	};
}
