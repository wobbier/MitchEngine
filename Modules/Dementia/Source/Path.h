#pragma once
#include <string>

class Path
{
public:
	Path() = default;

	explicit Path(const std::string& InFile, bool Raw = false);

	~Path();

	const char* GetExtension() const;

	bool IsFile = false;
	bool IsFolder = false;
	bool Exists = false;
	int8_t ExtensionPos;
	std::string FullPath;
	std::string LocalPath;
	std::string Directory;
};
