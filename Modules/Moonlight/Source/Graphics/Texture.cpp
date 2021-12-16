#include <iostream>
#include <assert.h>
#include "Texture.h"
#include "CLog.h"
#include "Utils/StringUtils.h"
#include <bimg/bimg.h>
#include "Utils/BGFXUtils.h"
#include <Utils/PlatformUtils.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <Utils/HavanaUtils.h>
#include <Device/FrameBuffer.h>

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
		//Load();
	}

	Texture::Texture(Moonlight::FrameBuffer* InFilePath, WrapMode mode /*= WrapMode::Wrap*/)
		: Resource(Path(""))
		, TexHandle(BGFX_INVALID_HANDLE)
	{
	}

	Texture::~Texture()
	{
		// TODO: Unload textures
	}

	void Texture::Load()
	{
		uint64_t flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_W_MIRROR;

		if (!FilePath.Exists)
		{
			return;
		}

		const auto* memory = Moonlight::LoadMemory(Path(FilePath.FullPath + ".dds"));
		if (memory)
		{
			if (bimg::ImageContainer* imageContainer = bimg::imageParse(Moonlight::getDefaultAllocator(), memory->data, memory->size))
			{
				const bgfx::Memory* mem = bgfx::makeRef(imageContainer->m_data, imageContainer->m_size, NULL, imageContainer);

				BX_FREE(Moonlight::getDefaultAllocator(), (void*)memory);
				if (imageContainer->m_cubeMap)
				{
					YIKES("You gotta implement cubemap textures");
				}
				else if (1 < imageContainer->m_depth)
				{
					YIKES("You gotta implement 3d textures");
				}
				else if (bgfx::isTextureValid(0, false, imageContainer->m_numLayers, bgfx::TextureFormat::Enum(imageContainer->m_format), flags))
				{
					TexHandle = bgfx::createTexture2D(imageContainer->m_width, imageContainer->m_height, 1 < imageContainer->m_numMips, imageContainer->m_numLayers, bgfx::TextureFormat::Enum(imageContainer->m_format), flags, mem);
				}

				if (bgfx::isValid(TexHandle))
				{
					bgfx::setName(TexHandle, FilePath.LocalPath.c_str());
				}

				bgfx::TextureInfo* info = nullptr;
				if (info)
				{
					bgfx::calcTextureSize(*info, imageContainer->m_width, imageContainer->m_height, imageContainer->m_depth, imageContainer->m_cubeMap, imageContainer->m_numMips > 0, imageContainer->m_numLayers, bgfx::TextureFormat::Enum(imageContainer->m_format));
				}
				mWidth = imageContainer->m_width;
				mHeight = imageContainer->m_height;
			}

		}
	}

	void Texture::Reload()
	{
		if (bgfx::isValid(TexHandle))
		{
			bgfx::destroy(TexHandle);
			TexHandle = BGFX_INVALID_HANDLE;
		}
		Load();
	}

	void Texture::UpdateBuffer(FrameBuffer* NewBuffer)
	{
		TexHandle = NewBuffer->Texture;
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

void TextureResourceMetadata::OnSerialize(json& outJson)
{
	outJson["Type"] = FromEnum(OutputType);
	outJson["MIPs"] = (OutputType == OutputTextureType::Sprite) ? GenerateSpriteMIPs : GenerateMIPs;
}

void TextureResourceMetadata::OnDeserialize(const json& inJson)
{
    if(!std::filesystem::exists(FilePath.FullPath + ".dds"))
    {
        FlaggedForExport = true;
    }

	if (inJson.contains("Type"))
	{
		OutputType = ToEnum(inJson["Type"]);
	}

	if (inJson.contains("MIPs"))
	{
		if (OutputType == OutputTextureType::Sprite)
		{
			GenerateSpriteMIPs = inJson["MIPs"];
		}
		else
		{
			GenerateMIPs = inJson["MIPs"];
		}
	}
}

std::string TextureResourceMetadata::FromEnum(OutputTextureType inType)
{
	switch (inType)
	{
	case OutputTextureType::NormalMap:
		return "Normal Map";
	case OutputTextureType::Sprite:
		return "Sprite";
	case OutputTextureType::Default:
	default:
		return "Default";
		break;
	}
}

TextureResourceMetadata::OutputTextureType TextureResourceMetadata::ToEnum(const std::string& inType)
{
	for (int n = 0; n < (int)OutputTextureType::Count; n++)
	{
		if (FromEnum((OutputTextureType)n) == inType)
		{
			return (OutputTextureType)n;
		}
	}

	return OutputTextureType::Default;
}

std::string TextureResourceMetadata::GetExtension2() const
{
	return std::string("dds");
}

#if ME_EDITOR

void TextureResourceMetadata::OnEditorInspect()
{
	MetaBase::OnEditorInspect();

	HavanaUtils::Label("Import Settings");
	if (ImGui::BeginCombo("##ImportSettings", FromEnum(OutputType).c_str()))
	{
		for (int n = 0; n < (int)OutputTextureType::Count; n++)
		{
			if (ImGui::Selectable(FromEnum((OutputTextureType)n).c_str(), false))
			{
				OutputType = (OutputTextureType)n;

				//static_cast<RenderCore*>(GetEngine().GetWorld().lock()->GetCore(RenderCore::GetTypeId()))->UpdateMesh(this);

				break;
			}
		}
		ImGui::EndCombo();
	}

	bool* genMips = &GenerateMIPs;
	if (OutputType == OutputTextureType::Sprite)
	{
		genMips = &GenerateSpriteMIPs;
	}
	ImGui::Checkbox("Generate MIPs", genMips);
}

#endif

#if ME_EDITOR || defined(ME_TOOLS)

void TextureResourceMetadata::Export()
{
	std::string exportType = " --as dds";
	if ((OutputType == OutputTextureType::Sprite && GenerateSpriteMIPs) || (OutputType != OutputTextureType::Sprite && GenerateMIPs))
	{
		exportType += " -m";
	}
	if (OutputType == OutputTextureType::NormalMap)
	{
		exportType += " -n";
	}
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

#if ME_PLATFORM_WIN64
	Path optickPath = Path("Engine/Tools/Win64/texturec.exe");
	if (!optickPath.Exists)
	{
		optickPath = Path("texturec.exe");
	}

	std::string progArgs = "-f \"";
	progArgs += FilePath.FullPath;
	progArgs += "\" -o \"" + FilePath.FullPath + ".dds\"" + exportType;
	// ./shaderc -f ../../../Assets/Shaders/vs_cubes.shader -o ../../../Assets/Shaders/dummy.bin --varyingdef ./varying.def.sc --platform windows -p vs_5_0 --type vertex

	PlatformUtils::SystemCall(optickPath, progArgs);
#else
    
    Path optickPath = Path("Engine/Tools/macOS/texturec");

    // texturec -f $in -o $out -t bc2 -m
    std::string progArgs = "\"" + optickPath.FullPath + "\" -f ../../";
    progArgs += FilePath.LocalPath;
    progArgs += " -o \"../../" + FilePath.LocalPath + ".dds\"" + exportType;
    system(progArgs.c_str());
    
#endif
}

#endif
