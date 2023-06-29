#pragma once

#include "ECS/Entity.h"
#include "ECS/Core.h"
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "imgui.h"
#include "Utils/HavanaUtils.h"

class SelfDestruct final
    : public Component<SelfDestruct>
{
public:
    float Lifetime = 5.0f;

    SelfDestruct()
        : Component( "SelfDestruct" )
    {
    }

    SelfDestruct( float InLifetime )
        : Component( "SelfDestruct" )
        , Lifetime( InLifetime )
    {
    }

    virtual void OnSerialize( json& outJson ) final
    {
    }

    virtual void OnDeserialize( const json& inJson ) final
    {
    }

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final
    {
        HavanaUtils::Label( "Lifetime" );
        ImGui::DragFloat( "##Lifetime", &Lifetime );
    }
#endif

    virtual void Init() final
    {
    }
};

ME_REGISTER_COMPONENT( SelfDestruct )

class SelfDestructor
    : public Core<SelfDestructor>
{
public:
    SelfDestructor()
        : Base( ComponentFilter().Requires<SelfDestruct>() )
    {
    }

    virtual void Update( const UpdateContext& context ) final;
};

ME_REGISTER_CORE( SelfDestructor )