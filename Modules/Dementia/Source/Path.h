#pragma once
#include <string>
#include <algorithm>
#include "Dementia.h"
#include "CLog.h"

#if USING( ME_PLATFORM_UWP )
#include <wrl/client.h>
#include <fstream>
#else
#include <filesystem>
#endif

class Path
{
public:
    Path() = default;

    explicit Path( const std::string& InFile, bool Raw = false );

    ~Path();

    const char* GetExtension() const;

    std::string_view GetDirectory() const;
    std::string GetDirectoryString() const;

    std::string_view GetLocalPath() const;
    std::string GetLocalPathString() const;

    std::string_view GetFileName() const;
    std::string GetFileNameString() const;

    bool IsFile = false;
    bool IsFolder = false;
    bool Exists = false;
    int8_t ExtensionPos;
    int8_t DirectoryPos;
    int8_t LocalPos;
    std::string FullPath;
};
