#pragma once
#include <string>

class ScriptHost
{
public:
    ScriptHost() = default;
    // Nothing at static destruction: hostfxr's own statics may be gone by then. The engine calls
    // Shutdown during its shutdown instead.
    ~ScriptHost() = default;

    // inDotnetRoot: a private .NET install shipped with the game (Build Game puts one in
    // "<exe dir>/dotnet": host/fxr/<version>/ and shared/Microsoft.NETCore.App/<version>/).
    // Empty: the system install (DOTNET_ROOT, then the OS default locations).
    bool Init( const std::string& inRuntimeConfigPath, const std::string& inDotnetRoot = std::string() );
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
