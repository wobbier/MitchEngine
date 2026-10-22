// not actually auto generated, but could be a fun project for the future
#pragma once
#include <numbers>

#include "ECS/EntityID.h"
#include "Math/Vector3.h"

struct ScriptEngineAPI
{
    // Core
    void  ( *Log )( const uint8_t* inMsg );
    float ( *GetTime )( );

    // Entity
    bool ( *Entity_IsAlive )( EntityID inId );
    bool ( *Entity_HasComponent )( EntityID inId, const uint8_t* inComponent );
    void ( *Entity_AddComponent )( EntityID inId, const uint8_t* inComponent );
    void ( *Transform_GetTranslation )( EntityID inId, Vector3* outTranslation );
    void ( *Transform_SetTranslation )( EntityID inId, const Vector3* inTranslation );

    // ImGui ( inOutValue is read for display, written back on edit )
    bool ( *ImGui_Float )( const uint8_t* inLabel, float* inOutValue );
    bool ( *ImGui_Int )( const uint8_t* inLabel, int* inOutValue );
    bool ( *ImGui_Bool )( const uint8_t* inLabel, uint8_t* inOutValue );
    bool ( *ImGui_Vec3 )( const uint8_t* inLabel, Vector3* inOutValue );
    bool ( *ImGui_Begin )( const uint8_t* inLabel );
    void ( *ImGui_Text )( const uint8_t* inText );
    bool ( *ImGui_Checkbox )( const uint8_t* inLabel, uint8_t* inOutValue );
    bool ( *ImGui_Button )( const uint8_t* inLabel );
    void ( *ImGui_End )( );

    // Input
    bool ( *Input_IsKeyDown )( int inKey );

    // World
    void ( *World_CreateEntity )( const uint8_t* inName, EntityID* outId );
    void ( *World_FindByName )( const uint8_t* inName, EntityID* outId );

    // Camera
    void ( *Camera_GetClearColor )( EntityID inId, Vector3* outColor );
    void ( *Camera_SetClearColor )( EntityID inId, const Vector3* inColor );

    // Transform
    void ( *Transform_GetScale )( EntityID inId, Vector3* outScale );
    void ( *Transform_SetScale )( EntityID inId, const Vector3* inScale );
    void ( *Transform_GetRotation )( EntityID inId, Vector3* outEuler );
    void ( *Transform_SetRotation )( EntityID inId, const Vector3* inEuler );

    // UI
    void ( *BasicUIView_ExecuteJS )( EntityID inId, const uint8_t* inJS );
};