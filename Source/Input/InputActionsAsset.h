#pragma once
#include "Input/InputActions.h"
#include "Resource/MetaFile.h"
#include "Resource/MetaRegistry.h"
#include "Resource/Resource.h"
#include <cstdint>

// A .inputactions asset: a JSON InputActionMap. Reloads when the file changes (Version counts the
// loads, so Input re-applies it).
class InputActionsResource
    : public Resource
{
public:
    explicit InputActionsResource( const Path& InPath );

    bool Load() override;
    void Reload() override;

    InputActionMap Map;
    uint32_t Version = 0;
};

// Edited in the asset browser: contexts, actions and bindings are drawn by the reflection UI and
// Save writes them back (which hot reloads the running game's actions).
struct InputActionsMetadata
    : public MetaBase
{
    explicit InputActionsMetadata( const Path& InPath )
        : MetaBase( InPath )
    {
    }

    std::string GetExtension2() const override
    {
        return "inputactions";
    }
    void OnSerialize( json& OutJson ) override
    {
    }
    void OnDeserialize( const json& InJson ) override
    {
    }

    const Reflection::TypeInfo* GetEditableType() override;
    void* GetEditableData() override;
    void SaveEditableData() override;

private:
    InputActionMap m_map;
    bool m_loaded = false;
};

ME_REGISTER_METADATA( "inputactions", InputActionsMetadata );
