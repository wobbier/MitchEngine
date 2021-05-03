#pragma once
#include "Resource/Resource.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include "Resource/MetaRegistry.h"
#include <string>
#include "Utils/StringUtils.h"
#include "Utils/BGFXUtils.h"
#include "bx/readerwriter.h"
#include <Utils/PlatformUtils.h>
#include <bgfx/bgfx.h>

namespace Moonlight
{
	class ShaderFile
		: public Resource
	{
	public:
		ShaderFile(const Path& InPath)
			: Resource(InPath)
		{
			std::string path = FilePath.FullPath.substr(FilePath.FullPath.rfind("/") + 1, FilePath.FullPath.length());
			std::string fullPath = FilePath.Directory + path + "." + Moonlight::GetPlatformString() + ".bin";
			//fullPath = fullPath.substr(0, fullPath.rfind(".")) + ".bin";
			Data = Moonlight::LoadMemory(Path(fullPath));
			Handle = bgfx::createShader(Data);
			bgfx::setName(Handle, InPath.LocalPath.c_str());
		}

		inline std::vector<char> ReadToByteArray(const char* filename)
		{
			std::vector<char> data;
			std::ifstream file(filename, std::ios::in | std::ios::binary | std::ios::ate);
			data.resize(file.tellg());
			file.seekg(0, std::ios::beg);
			file.read(&data[0], data.size());
			return data;
		}

		const bgfx::Memory* Data = nullptr;
		bgfx::ShaderHandle Handle = BGFX_INVALID_HANDLE;
	};
}

struct ShaderFileMetadata
	: public MetaBase
{
	ShaderFileMetadata(const Path& filePath)
		: MetaBase(filePath)
	{

	}

	void OnSerialize(json& outJson) override
	{
	}

	void OnDeserialize(const json& inJson) override
	{
	}

	void Export() override
	{
#if ME_PLATFORM_WIN64
		Path shadercPath = Path("Engine/Tools/Win64/shaderc.exe");

		std::string exportType;
		std::string shaderType;
		if (FilePath.Extension == "frag")
		{
			exportType = "fragment";
			shaderType = "ps_4_0";
		}
		else if (FilePath.Extension == "vert")
		{
			exportType = "vertex";
			shaderType = "vs_4_0";
		}

		std::string fileName = FilePath.LocalPath.substr(FilePath.LocalPath.rfind("/") + 1, FilePath.LocalPath.length());
		std::string localFolder = FilePath.LocalPath.substr(0, FilePath.LocalPath.rfind("/") + 1);

		// --platform windows -p vs_5_0 -O 3 --type vertex --depends -o $(@) -f $(<) --disasm
		//
		std::string nameNoExt = fileName.substr(0, fileName.rfind("."));
		std::string progArgs = "-f ../../../";
		progArgs += localFolder + fileName;
		progArgs += " -o ../../../" + localFolder + fileName + "." + Moonlight::GetPlatformString() + ".bin --varyingdef ../../../" + localFolder + nameNoExt + ".var --platform windows -p " + shaderType + " --type " + exportType;
		// ./shaderc -f ../../../Assets/Shaders/vs_cubes.shader -o ../../../Assets/Shaders/dummy.bin --varyingdef ./varying.def.sc --platform windows -p vs_5_0 --type vertex

		PlatformUtils::SystemCall(shadercPath, progArgs);

#elif ME_PLATFORM_MACOS
        Path shadercPath = Path("Engine/Tools/macOS/shaderc");
        
        std::string exportType;
        std::string shaderType;
        if (FilePath.Extension == "frag")
        {
            exportType = "fragment";
            shaderType = "ps_5_0";
        }
        else if (FilePath.Extension == "vert")
        {
            exportType = "vertex";
            shaderType = "vs_5_0";
        }

        std::string fileName = FilePath.LocalPath.substr(FilePath.LocalPath.rfind("/") + 1, FilePath.LocalPath.length());
        
        std::string localFolder = FilePath.LocalPath.substr(0, FilePath.LocalPath.rfind("/") + 1);
        
        std::string nameNoExt = fileName.substr(0, fileName.rfind("."));
        std::string progArgs = "\"" + shadercPath.FullPath + "\" -f ../../";
        progArgs += localFolder + fileName;
        progArgs += " -o ../../" + localFolder + fileName + "." + Moonlight::GetPlatformString() + ".bin --varyingdef ../../" + localFolder + nameNoExt + ".var --platform osx -p metal --depends -disasm --type " + exportType;
        
        

        // texturec -f $in -o $out -t bc2 -m
        system(progArgs.c_str());
#endif
	}
#if ME_EDITOR
	void OnEditorInspect() override
	{
	}
#endif
};

struct FragShaderFileMetadata
	: public ShaderFileMetadata
{
	FragShaderFileMetadata(const Path& filePath)
		: ShaderFileMetadata(filePath)
	{
	}
};

ME_REGISTER_METADATA("vert", ShaderFileMetadata);
ME_REGISTER_METADATA("frag", FragShaderFileMetadata);
