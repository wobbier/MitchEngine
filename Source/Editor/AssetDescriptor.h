#pragma once
#include <File.h>
#include "Types/AssetType.h"

struct AssetDescriptor
{
    static constexpr char* kDragAndDropPayload = "DND_ASSET_BROWSER";

    std::string Name;
    File MetaFile;
    Path FullPath;
    AssetType Type;
    long LastModified = 0l;
    std::string LastModifiedHuman;
    int ID = 0;
};