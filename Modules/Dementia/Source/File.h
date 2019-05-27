#pragma once
#include "FilePath.h"
#include <fstream>
#include "Logger.h"

class File
{
public:
	File() = default;
	File(const FilePath& path)
		: Path(path)
	{
	}

	const std::string& Read()
	{
		if (!Data.empty())
		{
			return Data;
		}
		std::fstream FileStream;

		FileStream.open(Path.FullPath.c_str(), std::ios::in);

		if (!FileStream)
		{
			Logger::Log(Logger::LogType::Error, "File IO: Failed to load file " + Path.LocalPath);
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
		std::ofstream out(Path.FullPath);
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
	FilePath Path;
private:

	bool IsOpen = false;
};