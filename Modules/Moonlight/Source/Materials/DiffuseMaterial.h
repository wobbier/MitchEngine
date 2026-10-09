#pragma once
#include "Materials/StandardMaterial.h"

// Pre-PBR material names, kept so existing scenes, prefabs and .mat files load. Both render with
// the Standard (PBR) shader; DiffuseColor is the base colour.
class DiffuseMaterial
    : public StandardMaterial
{
public:
    DiffuseMaterial()
        : StandardMaterial( "DiffuseMaterial" )
    {
    }

    SharedPtr<Material> CreateInstance() final
    {
        return MakeShared<DiffuseMaterial>( *this );
    }
};

class WhiteMaterial
    : public StandardMaterial
{
public:
    WhiteMaterial()
        : StandardMaterial( "WhiteMaterial" )
    {
    }

    SharedPtr<Material> CreateInstance() final
    {
        return MakeShared<WhiteMaterial>( *this );
    }
};

ME_REGISTER_MATERIAL_NAME( DiffuseMaterial, "Diffuse (legacy)" )
ME_REGISTER_MATERIAL_NAME( WhiteMaterial, "White (legacy)" )
