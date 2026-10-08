#include "PCH.h"
#include "ImGui.bindings.h"

#if USING( ME_SCRIPTING )
#include "imgui.h"

static bool Eng_ImGui_Float( const uint8_t* inLabel, float* inOutValue )
{
#if USING( ME_EDITOR )
    return HavanaUtils::Float( reinterpret_cast<const char*>( inLabel ), *inOutValue );
#else
    return false;
#endif
}


static bool Eng_ImGui_Int( const uint8_t* inLabel, int* inOutValue )
{
#if USING( ME_EDITOR )
    return HavanaUtils::Int( reinterpret_cast<const char*>( inLabel ), *inOutValue );
#else
    return false;
#endif
}


static bool Eng_ImGui_Bool( const uint8_t* inLabel, uint8_t* inOutValue )
{
#if USING( ME_EDITOR )
    bool b = ( *inOutValue != 0 );
    bool changed = ImGui::Checkbox( reinterpret_cast<const char*>( inLabel ), &b );
    *inOutValue = b ? 1 : 0;
    return changed;
#else
    return false;
#endif
}


static bool Eng_ImGui_Vec3( const uint8_t* inLabel, Vector3* inOutValue )
{
#if USING( ME_EDITOR )
    return HavanaUtils::EditableVector3( reinterpret_cast<const char*>( inLabel ), *inOutValue );
#else
    return false;
#endif
}


static bool Eng_ImGui_Begin( const uint8_t* inLabel )
{
    return ImGui::Begin( reinterpret_cast<const char*>( inLabel ) );
}


static void Eng_ImGui_End()
{
    ImGui::End();
}


static void Eng_ImGui_Text( const uint8_t* inText )
{
    ImGui::TextUnformatted( reinterpret_cast<const char*>( inText ) );
}


static bool Eng_ImGui_Checkbox( const uint8_t* inLabel, uint8_t* inOutValue )
{
    bool b = ( *inOutValue != 0 );
    bool changed = ImGui::Checkbox( reinterpret_cast<const char*>( inLabel ), &b );
    *inOutValue = b ? 1 : 0;
    return changed;
}


static bool Eng_ImGui_Button( const uint8_t* inLabel )
{
    return ImGui::Button( reinterpret_cast<const char*>( inLabel ) );
}


void Register_ImGuiBindings( ScriptEngineAPI& inAPI )
{
    inAPI.ImGui_Float    = Eng_ImGui_Float;
    inAPI.ImGui_Int      = Eng_ImGui_Int;
    inAPI.ImGui_Bool     = Eng_ImGui_Bool;
    inAPI.ImGui_Vec3     = Eng_ImGui_Vec3;
    inAPI.ImGui_Begin    = Eng_ImGui_Begin;
    inAPI.ImGui_Text     = Eng_ImGui_Text;
    inAPI.ImGui_Checkbox = Eng_ImGui_Checkbox;
    inAPI.ImGui_Button   = Eng_ImGui_Button;
    inAPI.ImGui_End      = Eng_ImGui_End;
}

#endif
