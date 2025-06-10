#include "PCH.h"
#include "ImGui.bindings.h"

#include <mono/metadata/object.h>
#include "imgui.h"

static bool ImGui_Begin( MonoString* inString )
{
    char* str = mono_string_to_utf8( inString );
    bool ret = ImGui::Begin( str );
    mono_free( str );
    return ret;
}


static void ImGui_End()
{
    ImGui::End();
}


static void ImGui_Text( MonoString* inString )
{
    char* str = mono_string_to_utf8( inString );
    ImGui::Text( str );
    mono_free( str );
}


static void ImGui_Checkbox( MonoString* inString, MonoBoolean* inValue )
{
    char* str = mono_string_to_utf8( inString );
    bool checkbox = *inValue;
    if( ImGui::Checkbox( str, &checkbox ) )
    {
        *inValue = checkbox;
    }
    mono_free( str );
}


static bool ImGui_Button( MonoString* inString, MonoBoolean* inValue )
{
    char* str = mono_string_to_utf8( inString );
    bool result = ImGui::Button( str );
    mono_free( str );
    return result;
}


void Register_ImGuiBindings()
{
    mono_add_internal_call( "ImGui::ImGui_Begin", (void*)ImGui_Begin );
    mono_add_internal_call( "ImGui::ImGui_End", (void*)ImGui_End );
    mono_add_internal_call( "ImGui::ImGui_Text", (void*)ImGui_Text );
    mono_add_internal_call( "ImGui::ImGui_Checkbox", (void*)ImGui_Checkbox );
    mono_add_internal_call( "ImGui::ImGui_Button", (void*)ImGui_Button );
}
