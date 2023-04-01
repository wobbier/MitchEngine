#pragma once

#if USING( ME_SCRIPTING )

#include "ECS/ComponentDetail.h"
#include "ECS/Component.h"
#include "Scripting/ScriptEngine.h"
#include "Math/Vector3.h"

// TODO: this should go
#include "Engine/Input.h"
#include <mono/metadata/object.h>

class ScriptComponent
    : public Component<ScriptComponent>
{
    friend class ScriptCore;
public:
    ScriptComponent();
    ~ScriptComponent();

    void Init() override;

    void DrawValues( const ScriptClass& scriptClass );

#if USING( ME_EDITOR )
    void OnEditorInspect() override;
#endif

private:
    void OnSerialize( json& outJson ) override;
    void OnDeserialize( const json& inJson ) override;


    // this should go
    static void Transform_GetTranslation( EntityID id, Vector3* outPosition );
    static void Transform_SetTranslation( EntityID id, Vector3* inPos );
    static bool Entity_HasComponent( EntityID id, MonoReflectionType* inType );
    static bool Input_IsKeyDown( KeyCode key );

    SharedPtr<ScriptInstance> Instance = nullptr;
    std::string ScriptName;
};

ME_REGISTER_COMPONENT( ScriptComponent )

#endif
