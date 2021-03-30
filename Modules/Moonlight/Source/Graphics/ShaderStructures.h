#pragma once
#include "RenderCommands.h"
#include "bgfx/bgfx.h"

namespace Moonlight
{
	struct PosColorVertex
	{
		Vector3 vec;
		uint32_t m_abgr;

		static void Init()
		{
			ms_layout
				.begin()
				.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
				.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
				.end();
		};

		static bgfx::VertexLayout ms_layout;
	};

	struct PosTexCoordVertex
	{
		Vector3 vec;
		Vector2 coords;

		static void Init()
		{
			ms_layout
				.begin()
				.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
				.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
				.end();
		};

		static bgfx::VertexLayout ms_layout;
	};

	struct PosNormTexTanBiVertex
	{
		Vector3 Position;
		Vector3 Normal;
		Vector2 TextureCoord;
		Vector3 Tangent;
		Vector3 BiTangent;

		static void Init()
		{
			ms_layout
				.begin()
				.add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
				.add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float, true)
				.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
				.add(bgfx::Attrib::Tangent,   3, bgfx::AttribType::Float)
				.add(bgfx::Attrib::Bitangent, 3, bgfx::AttribType::Float)
				.end();
		};

		static bgfx::VertexLayout ms_layout;
	};

	struct ShaderProgram
	{
		float test;
		/*Microsoft::WRL::ComPtr<ID3D11VertexShader> VertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> PixelShader;
		Microsoft::WRL::ComPtr<ID3D11InputLayout> InputLayout;*/
	};
}
