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
    // Update scheduling (ScriptCore): [ExecutionOrder], the callbacks the class implements, and the
    // handle / reload generation they were read for; scripts with equal orders run by start order.
    int m_executionOrder = 0;
    uint32_t m_callbacks = 0;
    int m_scheduleHandle = -1;
    uint32_t m_scheduleGeneration = 0;
    uint64_t m_startSequence = 0;
    // Reached an update since it started: a script started mid-frame (Play pressed after the update
    // stage) gets its first OnLateUpdate only after its first OnUpdate.
    bool m_hadUpdate = false;
    // saved variables from scene or entity.
    std::string m_savedFields;

    // Creates the C# instance (with the saved fields) if there isn't a live one.
    void EnsureCreated();
    void DestroyInstance();
#endif

    std::string ScriptName;
};

ME_REGISTER_COMPONENT( ScriptComponent )
