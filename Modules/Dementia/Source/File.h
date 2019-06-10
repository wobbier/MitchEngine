#pragma once
#include "Path.h"
#include <fstream>
#include "Logger.h"

class File
{
public:
	File() = default;
	File(const Path& path)
		: FilePath(path)
	{
	}

	const std::string& Read()
	{
		if (!Data.empty())
		{
			return Data;
		}
		std::fstream FileStream;

		FileStream.open(FilePath.FullPath.c_str(), std::ios::in);

		if (!FileStream)
		{
			Logger::Log(Logger::LogType::Error, "File IO: Failed to load file " + FilePath.LocalPath);
			return Data;
		}

		IsOpen = true;

		Data.assign((std::istreambuf_iterator<char>(FileStream)), (std::istreambuf_iterator<char>()));

		FileStream.close();
		IsOpen = false;

		return Data;
	}

	void Write()
	{
		std::ofstream out(FilePath.FullPath);
		out << Data;
		out.close();
	}

	void Write(const std::string& Contents)
	{
		Data = Contents;
		Write();
	}

	void Reset()
	{
		Data.clear();
	}

	std::string Data;
	Path FilePath;
private:

	bool IsOpen = false;
};