#pragma once
#include "FilePath.h"
#include <fstream>

class File
{
public:
	File() = default;
	File(const FilePath& path)
		: Path(path)
	{
	}

	void Open()
	{
		if (IsOpen)
		{
			return;
		}

		FileStream.open(Path.FullPath.c_str(), std::ios::in);

		if (!FileStream)
		{
		}

		IsOpen = true;

		Data.assign((std::istreambuf_iterator<char>(FileStream)), (std::istreambuf_iterator<char>()));
	}

	void Close()
	{
		if (IsOpen)
		{
			FileStream.close();
			IsOpen = false;
		}
	}

	const std::string& Read()
	{
		Open();
		Close();

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
	std::string Data;
	FilePath Path;
private:

	bool IsOpen = false;
	std::fstream FileStream;
};