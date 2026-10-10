#pragma once
#include "JSON.h"
#include "Dementia.h"
#include <filesystem>
#include <time.h>
#include <chrono>
using namespace std::chrono_literals;

#if USING( ME_EDITOR )
#include <imgui.h>
#endif
#include "Path.h"

namespace Reflection { struct TypeInfo; }

struct MetaBase
{
    MetaBase() = delete;

    MetaBase( const Path& filePath );
    virtual ~MetaBase() = default;

    void Serialize( json& outJson );
    void Deserialize( const json& inJson );

    virtual void Export() {};
    // Hot reload: true when Export is slow (a compiler run) and safe off the main thread, so a
    // changed asset recompiles on a reimport thread and reloads when it's done.
    virtual bool ExportsInBackground() const
    {
        return false;
    }

    virtual std::string GetExtension2() const = 0;

    void Save();

    virtual void OnSerialize( json& outJson ) = 0;
    virtual void OnDeserialize( const json& inJson ) = 0;

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() {
        ImGui::Text( "File Type: " );
        ImGui::SameLine();
        ImGui::Text( "%s", FileType.c_str() );
        ImGui::Text( "Last Modified: " );
        ImGui::SameLine();
        ImGui::Text( "%s", LastModifiedDebug.c_str() );
    }
#endif

    // Data assets: a reflected object the editor draws (and edits in place) in the asset's details,
    // written back to the asset by SaveEditableData.
    virtual const Reflection::TypeInfo* GetEditableType()
    {
        return nullptr;
    }
    virtual void* GetEditableData()
    {
        return nullptr;
    }
    virtual void SaveEditableData()
    {
    }

    std::string FileType;
    std::string LastModifiedDebug;


    Path FilePath;
    long LastModified = 0;
    bool FlaggedForExport = false;
    // Stable identity of the asset (generated the first time the meta is written).
    uint64_t GUID = 0;
};
