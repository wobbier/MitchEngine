#include <iostream>
#include <assert.h>
#include "Texture.h"
#include "Logger.h"
#include <WICTextureLoader.h>
#include "Game.h"
#include "Device/D3D12Device.h"
#include "Engine/Engine.h"

using namespace DirectX;
using namespace Microsoft::WRL;


namespace Moonlight
{
	Texture::Texture(const Path& InFilePath)
		: Resource(InFilePath)
	{
		std::wstring filePath = ToStringW(InFilePath.FullPath);
		auto& device = static_cast<Moonlight::D3D12Device&>(GetEngine().GetRenderer().GetDevice());
		ID3D11DeviceContext* context;
		device.GetD3DDevice()->GetImmediateContext(&context);

		DX::ThrowIfFailed(CreateWICTextureFromFile(device.GetD3DDevice(), filePath.c_str(), &resource, &CubesTexture));
		D3D11_SAMPLER_DESC sampDesc;
		ZeroMemory(&sampDesc, sizeof(sampDesc));
		sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sampDesc.MinLOD = 0;
		sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
		device.GetD3DDevice()->CreateSamplerState(&sampDesc, &CubesTexSamplerState);
	}

	Texture::~Texture()
	{
		// TODO: Unload textures
	}

	std::string Texture::ToString(TextureType type)
	{
		switch (type)
		{
		case TextureType::Diffuse:
			return "Diffuse";
		case TextureType::Normal:
			return "Normal";
		case TextureType::Specular:
			return "Specular";
		case TextureType::Height:
			return "Height";
		case TextureType::Opacity:
			return "Opacity";
		case TextureType::Count:
		default:
			Logger::GetInstance().Log(Logger::LogType::Error, "Couldn't find texture type: " + std::to_string(type));
			return "";
		}
	}

	std::wstring Texture::ToStringW(const std::string& strText)
	{
		std::wstring      wstrResult;
		wstrResult.resize(strText.length());
		typedef std::codecvt<wchar_t, char, mbstate_t> widecvt;
		std::locale     locGlob;
		std::locale::global(locGlob);
		const widecvt& cvt(std::use_facet<widecvt>(locGlob));
		mbstate_t   State;
		const char*       cTemp;
		wchar_t*    wTemp;
		cvt.in(State,
			&strText[0], &strText[0] + strText.length(), cTemp,
			(wchar_t*)&wstrResult[0], &wstrResult[0] + wstrResult.length(), wTemp);

		return wstrResult;
	}

}