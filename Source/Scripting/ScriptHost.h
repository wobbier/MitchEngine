#pragma once
#include <string>

class ScriptHost
{
public:
    ScriptHost() = default;
    ~ScriptHost()
    {
        Shutdown();
    }

    bool Init( const std::string& inRuntimeConfigPath );
    void Shutdown();

    // get [UnmanagedCallersOnly] function pointer.
    // inTypeName - "Namespace.Class, AssemblyName"
    bool LoadFunction( const std::string& inAssemblyPath,
        const std::string& inTypeName,
        const std::string& inMethodName,
        void** outFnPtr );

    inline bool IsInitialized() const
    {
        return m_loadAssemblyFn != nullptr;
    }


private:
    void* m_hostfxrLib = nullptr;
    void* m_hostfxrCtx = nullptr;
    void ( *m_closeFptr )( void* ) = nullptr;
    void* m_loadAssemblyFn = nullptr;
};
