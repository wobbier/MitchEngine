#pragma once
#include "ECS/ComponentDetail.h"
#include "ECS/Component.h"
#include "Scripting/ScriptEngine.h"
#include "Math/Vector3.h"

class ScriptComponent
    : public Component<ScriptComponent>
{
    friend class ScriptCore;
public:
    ScriptComponent();
    ~ScriptComponent();

    void Init() override;
    void OnEditorInspect() override;

private:
    void OnSerialize( json& outJson ) override;
    void OnDeserialize( const json& inJson ) override;

    static void Entity_GetTranslation( EntityID id, Vector3* outPosition );
    static void Entity_SetTranslation( EntityID id, Vector3* inPos );

    ScriptInstance* Instance = nullptr;
    std::string ScriptName;
};

ME_REGISTER_COMPONENT( ScriptComponent )