#pragma once
#include <File.h>
#include "Types/AssetType.h"

struct AssetDescriptor
{
    static constexpr const char* kDragAndDropPayload = "DND_ASSET_BROWSER";

    static AssetDescriptor* s_dragged;
    static AssetDescriptor* GetDragged() { return s_dragged; }

    std::string Name;
    File MetaFile;
    Path FullPath;
    AssetType Type;
    long LastModified = 0l;
    std::string LastModifiedHuman;
    int ID = 0;
};