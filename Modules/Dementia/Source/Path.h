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

    std::string_view GetFileName( bool inIncludeExt = true ) const;
    std::string GetFileNameString( bool inIncludeExt = true ) const;

    bool IsFile = false;
    bool IsFolder = false;
    bool Exists = false;
    size_t ExtensionPos = 0;
    size_t DirectoryPos = 0;
    size_t LocalPos = 0;
    std::string FullPath;
};
