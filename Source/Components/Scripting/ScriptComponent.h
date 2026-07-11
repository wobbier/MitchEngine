#pragma once

#include "ECS/ComponentDetail.h"
#include "ECS/Component.h"
#include "Scripting/ScriptEngine.h"


class ScriptComponent
    : public Component<ScriptComponent>
{
    friend class ScriptCore;
public:
    ScriptComponent();
    ~ScriptComponent();

    void Init() override;

#if USING( ME_EDITOR )
    void OnEditorInspect() override;
#endif

private:
    void OnSerialize( json& outJson ) override;
    void OnDeserialize( const json& inJson ) override;

#if USING( ME_SCRIPTING )
    int m_dotnetHandle = -1;
    // saved variables from scene or entity.
    std::string m_savedFields;
#endif

    std::string ScriptName;
};

ME_REGISTER_COMPONENT( ScriptComponent )
