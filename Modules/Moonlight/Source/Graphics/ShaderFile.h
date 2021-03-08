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

#if ME_PLATFORM_WIN64
#include <Windows.h>
#include <winnt.h>
#endif

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
		Path optickPath = Path("Engine/Tools/Win64/shaderc.exe");

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

		// --platform windows -p vs_5_0 -O 3 --type vertex --depends -o $(@) -f $(<) --disasm
		//
		std::string nameNoExt = fileName.substr(0, fileName.rfind("."));
		std::string srtt = "-f ../../../";
		srtt += localFolder + fileName;
		srtt += " -o ../../../" + localFolder + fileName + "." + Moonlight::GetPlatformString() + ".bin --varyingdef ../../../" + localFolder + nameNoExt + ".var --platform windows -p " + shaderType + " --type " + exportType;
		SetCurrentDirectory(StringUtils::ToWString(optickPath.Directory).c_str());
		// ./shaderc -f ../../../Assets/Shaders/vs_cubes.shader -o ../../../Assets/Shaders/dummy.bin --varyingdef ./varying.def.sc --platform windows -p vs_5_0 --type vertex

		STARTUPINFO si;
		PROCESS_INFORMATION pi;
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));

		if (!CreateProcessW(StringUtils::ToWString(optickPath.FullPath).c_str(), &StringUtils::ToWString(srtt)[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
		{
			printf("CreateProcess failed (%d).\n", GetLastError());
			throw std::exception("Could not create child process");
		}

		WaitForSingleObject(pi.hProcess, INFINITE);

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
#else
        Path optickPath = Path("Engine/Tools/macOS/shaderc");
        
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
        std::string srtt = "\"" + optickPath.FullPath + "\" -f ../../";
        srtt += localFolder + fileName;
        srtt += " -o ../../" + localFolder + fileName + "." + Moonlight::GetPlatformString() + ".bin --varyingdef ../../" + localFolder + nameNoExt + ".var --platform osx -p metal --type " + exportType;
        
        

        // texturec -f $in -o $out -t bc2 -m
        system(srtt.c_str());
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
