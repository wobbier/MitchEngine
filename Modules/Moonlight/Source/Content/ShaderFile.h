#pragma once
#include "Resource/Resource.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace Moonlight
{
	class ShaderFile
		: public Resource
	{
	public:
		ShaderFile(const FilePath& InPath)
			: Resource(InPath)
		{
			Data = ReadToByteArray(Path.FullPath.c_str());
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
		std::vector<char> Data;
	};
}