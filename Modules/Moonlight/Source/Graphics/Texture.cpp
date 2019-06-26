#include <iostream>
#include <assert.h>
#include "Texture.h"
#include "Logger.h"
#include <WICTextureLoader.h>
#include "Game.h"
#include "Device/D3D12Device.h"
#include "Engine/Engine.h"

#include "stb_image.cpp"

using namespace DirectX;
using namespace Microsoft::WRL;


namespace Moonlight
{
	Texture::Texture(const Path& InFilePath, WrapMode mode)
		: Resource(InFilePath)
	{
		std::wstring filePath = ToStringW(InFilePath.FullPath);
		auto& device = static_cast<Moonlight::D3D12Device&>(GetEngine().GetRenderer().GetDevice());
		ID3D11DeviceContext* context;
		device.GetD3DDevice()->GetImmediateContext(&context);

		/*
		stbi_uc* mData = stbi_load(InFilePath.FullPath.c_str(), &mWidth, &mHeight, &mChannels, STBI_rgb_alpha);
		//levels = (levels > 0) ? levels : NumMipmapLevels(mWidth, mHeight);

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = mWidth;
		desc.Height = mHeight;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		//if (levels == 0) {
			desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
			//desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
		//}

		D3D11_SUBRESOURCE_DATA textureData = {};
		textureData.pSysMem = (void*)mData;
		textureData.SysMemPitch = (32 * mWidth) / 8;
		textureData.SysMemSlicePitch = textureData.SysMemPitch * mHeight;

		ID3D11Texture2D* tex = nullptr;
		DX::ThrowIfFailed(device.GetD3DDevice()->CreateTexture2D(&desc, nullptr, &tex));
		*/

				UINT bindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
				UINT miscFlags = 0;
				//if (levels == 0)
				{
					bindFlags |= D3D11_BIND_RENDER_TARGET;
					miscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
				}
				DX::ThrowIfFailed(CreateWICTextureFromFileEx(device.GetD3DDevice(), context, filePath.c_str(), 2048, D3D11_USAGE_DEFAULT, bindFlags, NULL, miscFlags, NULL, &resource, &ShaderResourceView));
			/*

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		//srvDesc.Texture2D.MostDetailedMip = 3;
		//srvDesc.Texture2D.MipLevels = -1;
		DX::ThrowIfFailed(device.GetD3DDevice()->CreateShaderResourceView(tex, &srvDesc, &ShaderResourceView));

		context->UpdateSubresource(tex, 0, nullptr, mData, (mWidth * (mChannels * sizeof(unsigned char))), 0);

		//if (levels == 0)
		{
			//device.GetD3DDeviceContext()->GenerateMips(ShaderResourceView);
		}
		SamplerState = device.CreateSamplerState(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP);
		*/
				D3D11_TEXTURE_ADDRESS_MODE dxMode = D3D11_TEXTURE_ADDRESS_WRAP;
				switch (mode)
				{
				case Moonlight::Clamp:
				case Moonlight::Decal:
					dxMode = D3D11_TEXTURE_ADDRESS_CLAMP;
					break;
				case Moonlight::Wrap:
					dxMode = D3D11_TEXTURE_ADDRESS_WRAP;
					break;
				case Moonlight::Mirror:
					dxMode = D3D11_TEXTURE_ADDRESS_MIRROR;
					break;
				default:
					break;
				}
				D3D11_SAMPLER_DESC sampDesc;
				ZeroMemory(&sampDesc, sizeof(sampDesc));
				sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
				sampDesc.AddressU = dxMode;
				sampDesc.AddressV = dxMode;
				sampDesc.AddressW = dxMode;
				sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
				sampDesc.MinLOD = 0;
				sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
				device.GetD3DDevice()->CreateSamplerState(&sampDesc, &SamplerState);
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