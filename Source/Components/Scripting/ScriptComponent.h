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

    const std::string& GetScriptName() const
    {
        return ScriptName;
    }
    // The C# instance's handle (-1 without one).
    int GetHandle() const
    {
#if USING( ME_SCRIPTING )
        return m_dotnetHandle;
#else
        return -1;
#endif
    }

#if USING( ME_EDITOR )
    void OnEditorInspect() override;
#endif

private:
    void OnSerialize( json& outJson ) override;
    void OnDeserialize( const json& inJson ) override;

#if USING( ME_SCRIPTING )
    int m_dotnetHandle = -1;
    bool m_started = false;
    // saved variables from scene or entity.
    std::string m_savedFields;

    // Creates the C# instance (with the saved fields) if there isn't a live one.
    void EnsureCreated();
    void DestroyInstance();
#endif

    std::string ScriptName;
};

ME_REGISTER_COMPONENT( ScriptComponent )
