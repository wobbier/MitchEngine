#include <iostream>
#include <assert.h>
#include "Texture.h"
#include "CLog.h"
#include <WICTextureLoader.h>
#include "Game.h"
#include "Device/DX11Device.h"
#include "Engine/Engine.h"
#include "Utils/StringUtils.h"
#include <bimg/bimg.h>

using namespace DirectX;
using namespace Microsoft::WRL;

static void imageReleaseCb(void* _ptr, void* _userData)
{
	BX_UNUSED(_ptr);
	bimg::ImageContainer* imageContainer = (bimg::ImageContainer*)_userData;
	bimg::imageFree(imageContainer);
}

void* load(bx::FileReaderI* _reader, bx::AllocatorI* _allocator, const char* _filePath, uint32_t* _size)
{
	if (bx::open(_reader, _filePath))
	{
		uint32_t size = (uint32_t)bx::getSize(_reader);
		void* data = BX_ALLOC(_allocator, size);
		bx::read(_reader, data, size);
		bx::close(_reader);
		if (nullptr != _size)
		{
			*_size = size;
		}
		return data;
	}
	else
	{
		//DBG("Failed to open: %s.", _filePath);
	}

	if (nullptr != _size)
	{
		*_size = 0;
	}

	return nullptr;
}

void unload(bx::AllocatorI* _allocator, void* _ptr)
{
	BX_FREE(_allocator, _ptr);
}

namespace Moonlight
{
	Texture::Texture(const Path& InFilePath, WrapMode mode)
		: Resource(InFilePath)
	{
		//{
		//	uint64_t flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE;
		//	if (!s_fileReader)
		//	{
		//		s_fileReader = new Moonlight::FileReader();// BX_NEW(s_allocator, Moonlight::FileReader);
		//	}
		//	uint32_t size;
		//	void* data = load(s_fileReader, getDefaultAllocator(), InFilePath.FullPath.c_str(), &size);

		//	const auto bxAlloc = getDefaultAllocator();
		//	if (!bxAlloc) return;

		//	bimg::ImageContainer* imageContainer = new bimg::ImageContainer();
		//	bx::Error* err = nullptr;
		//	bimg::imageParse(
		//		*imageContainer, data, static_cast<uint32_t>(size));
		//	if (imageContainer == nullptr) return;

		//	const auto* const mem =
		//		bgfx::makeRef(imageContainer->m_data, imageContainer->m_size,
		//			imageReleaseCb, imageContainer);

		//	if (!bgfx::isTextureValid(
		//		0, false, imageContainer->m_numLayers,
		//		static_cast<bgfx::TextureFormat::Enum>(imageContainer->m_format),
		//		bgfx::TextureFormat::RGBA4))
		//		return;

		//	TexHandle = bgfx::createTexture2D(
		//		static_cast<uint16_t>(imageContainer->m_width),
		//		static_cast<uint16_t>(imageContainer->m_height),
		//		1 < imageContainer->m_numMips, imageContainer->m_numLayers,
		//		static_cast<bgfx::TextureFormat::Enum>(imageContainer->m_format),
		//		bgfx::TextureFormat::RGBA4, mem);

		//	//tex2D->_width = imageContainer->m_width;
		//	//tex2D->_height = imageContainer->m_height;

		//	if (!bgfx::isValid(TexHandle)) return;

		//	bgfx::setName(TexHandle, InFilePath.LocalPath.c_str());
		//}
		//return;
		uint64_t flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE;
		if (!s_fileReader)
		{
			s_fileReader = new Moonlight::FileReader();// BX_NEW(s_allocator, Moonlight::FileReader);
		}
			uint32_t size;
		void* data = load(s_fileReader, getDefaultAllocator(), InFilePath.FullPath.c_str(), &size);

		if (data)
		{
			bimg::ImageContainer* imageContainerRaw = new bimg::ImageContainer();
			bx::Error* err = nullptr;
			if (bimg::imageParse(*imageContainerRaw, data, size, err))
			{
				bimg::ImageContainer* imageContainer = imageContainerRaw;
				bgfx::TextureInfo info;
				bgfx::calcTextureSize(info, imageContainer->m_width, imageContainer->m_height, 1, false, false, 1, bgfx::TextureFormat::Enum(imageContainer->m_format));
				if (data != nullptr)
				{
					TexHandle = bgfx::createTexture2D(imageContainer->m_width, imageContainer->m_height, false, 1, bgfx::TextureFormat::Enum(imageContainer->m_format), flags, bgfx::copy(data, info.storageSize));
				}
				else
				{
					return;
		/*			m_texture = bgfx::createTexture2D(imageContainer->m_width, imageContainer->m_height, false, 1, format, flags);

					const bgfx::Memory* memory = bgfx::alloc(info.storageSize);
					memset(memory->data, 0, info.storageSize);
					bgfx::updateTexture2D(m_texture, 0, 0, 0, 0, width, height, memory, info.storageSize / height);*/
				}
				return;

				bimg::Orientation::Enum* orientation = nullptr;
				if (imageContainer)
				{
					if (orientation)
					{
						*orientation = imageContainer->m_orientation;
					}

					const bgfx::Memory* mem = bgfx::makeRef(data, size, imageReleaseCb, imageContainer);

					if (imageContainer->m_cubeMap)
					{
						TexHandle = bgfx::createTextureCube(imageContainer->m_width, 1 < imageContainer->m_numMips, imageContainer->m_numLayers, bgfx::TextureFormat::Enum(imageContainer->m_format), flags, mem);
					}
					else if (1 < imageContainer->m_depth)
					{
						TexHandle = bgfx::createTexture3D(imageContainer->m_width, imageContainer->m_height, imageContainer->m_depth, 1 < imageContainer->m_numMips, bgfx::TextureFormat::Enum(imageContainer->m_format), flags, mem);
					}
					else if (bgfx::isTextureValid(0, false, imageContainer->m_numLayers, bgfx::TextureFormat::Enum(imageContainer->m_format), flags))
					{
						imageContainer->m_data = data;
						imageContainer->m_size = size;
						imageContainer->m_allocator = getDefaultAllocator();
						TexHandle = bgfx::createTexture2D(uint16_t(imageContainer->m_width), uint16_t(imageContainer->m_height), 1 < imageContainer->m_numMips, imageContainer->m_numLayers, bgfx::TextureFormat::Enum(imageContainer->m_format), flags, mem);
					}

					if (bgfx::isValid(TexHandle))
					{
						bgfx::setName(TexHandle, InFilePath.LocalPath.c_str());
					}
					//unload(getDefaultAllocator(), data);
				}
			}

		}
		return;
		std::wstring filePath = StringUtils::ToWString(InFilePath.FullPath);
		auto& device = static_cast<Moonlight::DX11Device&>(GetEngine().GetRenderer().GetDevice());
		ID3D11DeviceContext* context;
		device.GetD3DDevice()->GetImmediateContext(&context);

		UINT bindFlags = D3D11_BIND_SHADER_RESOURCE/* | D3D11_BIND_UNORDERED_ACCESS*/;
		UINT miscFlags = 0;
		//if (levels == 0)
		{
			bindFlags |= D3D11_BIND_RENDER_TARGET;
			miscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
		}
		DX::ThrowIfFailed(CreateWICTextureFromFileEx(device.GetD3DDevice(), context, filePath.c_str(), 2048, D3D11_USAGE_DEFAULT, bindFlags, NULL, miscFlags, NULL, &resource, &ShaderResourceView));
		
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

	Texture::Texture(Moonlight::FrameBuffer* InFilePath, WrapMode mode /*= WrapMode::Wrap*/)
		: Resource(Path(""))
	{
		return;
		auto& device = static_cast<Moonlight::DX11Device&>(GetEngine().GetRenderer().GetDevice());

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
		if (InFilePath)
		{
			UpdateBuffer(InFilePath);
		}
	}

	Texture::~Texture()
	{
		// TODO: Unload textures
	}

	void Texture::UpdateBuffer(FrameBuffer* NewBuffer)
	{
		ShaderResourceView = NewBuffer->ShaderResourceView.Get();
	}

	bx::AllocatorI* Texture::getDefaultAllocator()
	{
		BX_PRAGMA_DIAGNOSTIC_PUSH();
		BX_PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4459); // warning C4459: declaration of 's_allocator' hides global declaration
		BX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wshadow");
		static bx::DefaultAllocator s_allocator;
		return &s_allocator;
		BX_PRAGMA_DIAGNOSTIC_POP();
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
			CLog::GetInstance().Log(CLog::LogType::Error, "Couldn't find texture type: " + std::to_string(type));
			return "";
		}
	}

}