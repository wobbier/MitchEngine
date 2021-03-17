#include <iostream>
#include <assert.h>
#include "Texture.h"
#include "CLog.h"
#include "Game.h"
#include "Device/DX11Device.h"
#include "Engine/Engine.h"
#include "Utils/StringUtils.h"
#include <bimg/bimg.h>
#include "Utils/BGFXUtils.h"
#include <Utils/PlatformUtils.h>

//using namespace DirectX;

static void imageReleaseCb(void* _ptr, void* _userData)
{
	BX_UNUSED(_ptr);
	bimg::ImageContainer* imageContainer = (bimg::ImageContainer*)_userData;
	bimg::imageFree(imageContainer);
}

void unload(bx::AllocatorI* _allocator, void* _ptr)
{
	BX_FREE(_allocator, _ptr);
}

namespace Moonlight
{
	Texture::Texture(const Path& InFilePath, WrapMode mode)
		: Resource(InFilePath)
        , TexHandle(BGFX_INVALID_HANDLE)
	{
		uint64_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

		//D3D11_TEXTURE_ADDRESS_MODE dxMode = D3D11_TEXTURE_ADDRESS_WRAP;
		//switch (mode)
		//{
		//case Moonlight::Clamp:
		//case Moonlight::Decal:
		//	dxMode = D3D11_TEXTURE_ADDRESS_CLAMP;
		//	break;
		//case Moonlight::Wrap:
		//	dxMode = D3D11_TEXTURE_ADDRESS_WRAP;
		//	break;
		//case Moonlight::Mirror:
		//	dxMode = D3D11_TEXTURE_ADDRESS_MIRROR;
		//	break;
		//default:
		//	break;
		//}

		const auto* memory = Moonlight::LoadMemory(Path(InFilePath.FullPath + ".dds"));
		if (memory)
		{
			bimg::ImageContainer* imageContainerRaw = new bimg::ImageContainer();
			bx::Error* err = nullptr;
			if (bimg::imageParse(*imageContainerRaw, memory->data, memory->size, err))
			{
				bimg::ImageContainer* imageContainer = imageContainerRaw;
				bgfx::TextureInfo info;
				bgfx::calcTextureSize(info, imageContainer->m_width, imageContainer->m_height, 1, false, false, 1, bgfx::TextureFormat::Enum(imageContainer->m_format));
				if (bgfx::isTextureValid(0, false, imageContainer->m_numLayers, bgfx::TextureFormat::Enum(imageContainer->m_format), flags))
				{
					TexHandle = bgfx::createTexture2D(imageContainer->m_width, imageContainer->m_height, false, 1, bgfx::TextureFormat::Enum(imageContainer->m_format), flags, bgfx::copy(memory->data, info.storageSize));
				}
				else
				{
					//m_texture = bgfx::createTexture2D(imageContainer->m_width, imageContainer->m_height, false, 1, format, flags);
					
					//const bgfx::Memory* memory = bgfx::alloc(info.storageSize);
					//memset(memory->data, 0, info.storageSize);
					//bgfx::updateTexture2D(m_texture, 0, 0, 0, 0, width, height, memory, info.storageSize / height);
				}
			}

		}
	}

	Texture::Texture(Moonlight::FrameBuffer* InFilePath, WrapMode mode /*= WrapMode::Wrap*/)
		: Resource(Path(""))
	{

	}

	Texture::~Texture()
	{
		// TODO: Unload textures
	}

	void Texture::UpdateBuffer(FrameBuffer* NewBuffer)
	{
		//ShaderResourceView = NewBuffer->ShaderResourceView.Get();
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

void TextureResourceMetadata::OnSerialize(json& inJson)
{

}

void TextureResourceMetadata::OnDeserialize(const json& inJson)
{
    if(!std::filesystem::exists(FilePath.FullPath + ".dds"))
    {
        FlaggedForExport = true;
    }
}

#if ME_EDITOR

void TextureResourceMetadata::OnEditorInspect()
{
	MetaBase::OnEditorInspect();

	static bool isChecked = false;
	ImGui::Checkbox("Texture Resource Test", &isChecked);
}

void TextureResourceMetadata::Export()
{
#if ME_PLATFORM_WIN64
	Path optickPath = Path("Engine/Tools/Win64/texturec.exe");

	/*
		rule texturec_bc1
			command = texturec -f $in -o $out -t bc1 -m
		rule texturec_bc2
			command = texturec -f $in -o $out -t bc2 -m
		rule texturec_bc3
			command = texturec -f $in -o $out -t bc3 -m
		rule texturec_bc4
			command = texturec -f $in -o $out -t bc4 -m
		rule texturec_bc5
			command = texturec -f $in -o $out -t bc5 -m
		rule texturec_bc7
			command = texturec -f $in -o $out -t bc7 -m
		rule texturec_etc1
			command = texturec -f $in -o $out -t etc1 -m
		rule texturec_etc2
			command = texturec -f $in -o $out -t etc2 -m
		rule texturec_diffuse
			command = texturec -f $in -o $out -t bc2 -m
		rule texturec_normal
			command = texturec -f $in -o $out -t bc5 -m -n
		rule texturec_height
			command = texturec -f $in -o $out -t r8
		rule texturec_equirect
			command = texturec -f $in -o $out --max 512 -t rgba16f --equirect
	*/

	std::string exportType = " --as dds";

	std::string fileName = FilePath.LocalPath.substr(FilePath.LocalPath.rfind("/") + 1, FilePath.LocalPath.length());

	std::string progArgs = "-f ../../../";
	progArgs += FilePath.LocalPath;
	progArgs += " -o ../../../" + FilePath.LocalPath + ".dds" + exportType;
	// ./shaderc -f ../../../Assets/Shaders/vs_cubes.shader -o ../../../Assets/Shaders/dummy.bin --varyingdef ./varying.def.sc --platform windows -p vs_5_0 --type vertex

	PlatformUtils::SystemCall(optickPath, progArgs);
#else
    
    Path optickPath = Path("Engine/Tools/macOS/texturec");
    std::string exportType = " --as dds";

    std::string fileName = FilePath.LocalPath.substr(FilePath.LocalPath.rfind("/") + 1, FilePath.LocalPath.length());
    // texturec -f $in -o $out -t bc2 -m
    std::string progArgs = "\"" + optickPath.FullPath + "\" -f ../../";
    progArgs += FilePath.LocalPath;
    progArgs += " -o \"../../" + FilePath.LocalPath + ".dds\"" + exportType;
    system(progArgs.c_str());
    
#endif
}

#endif
