#pragma once

#include "ECS/ComponentDetail.h"
#include "ECS/Component.h"
#include "Scripting/ScriptEngine.h"

// TODO: this should go
#include "Engine/Input.h"

#if USING( ME_SCRIPTING )
#include <mono/metadata/object.h>
#endif

class ScriptComponent
    : public Component<ScriptComponent>
{
    friend class ScriptCore;
public:
    ScriptComponent();
    ~ScriptComponent();

    void Init() override;

#if USING( ME_SCRIPTING )
    void DrawValues( const ScriptClass& scriptClass );
#endif

#if USING( ME_EDITOR )
    void OnEditorInspect() override;
#endif

private:
    void OnSerialize( json& outJson ) override;
    void OnDeserialize( const json& inJson ) override;


#if USING( ME_SCRIPTING )

    // this should go
    static void Transform_GetTranslation( EntityID id, Vector3* outPosition );
    static void Transform_SetTranslation( EntityID id, Vector3* inPos );
    static void Transform_GetScale( EntityID id, Vector3* outPosition );
    static void Transform_SetScale( EntityID id, Vector3* inPos );
    static void Camera_GetClearColor( EntityID id, Vector3* outPosition );
    static void Camera_SetClearColor( EntityID id, Vector3* inPos );
    static bool Entity_HasComponent( EntityID id, MonoReflectionType* inType );
    static bool Input_IsKeyDown( KeyCode key );

    SharedPtr<ScriptInstance> Instance = nullptr;
#endif
    std::string ScriptName;
};

ME_REGISTER_COMPONENT( ScriptComponent )
