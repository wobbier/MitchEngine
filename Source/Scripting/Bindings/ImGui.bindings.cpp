#include "PCH.h"
#include "ImGui.bindings.h"

#if USING( ME_SCRIPTING )
#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Scripting/ScriptInspectorRows.h"
#include "Scripting/Bindings/BindingContext.h"
#include "Engine/World.h"
#include "ECS/Entity.h"

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


// --- Script inspector rows (the same property-row layout as the C++ reflection inspector) ------

static bool Eng_ImGui_String( const uint8_t* inLabel, uint8_t* inOutUtf8, int inSize )
{
#if USING( ME_EDITOR )
    const char* label = reinterpret_cast<const char*>( inLabel );
    ImGui::PushID( label );
    HavanaUtils::Label( label );
    const bool changed = ImGui::InputText( "##text", reinterpret_cast<char*>( inOutUtf8 ), static_cast<size_t>( inSize ) );
    ImGui::PopID();
    return changed;
#else
    return false;
#endif
}


static bool Eng_ImGui_Multiline( const uint8_t* inLabel, uint8_t* inOutUtf8, int inSize, int inLines )
{
#if USING( ME_EDITOR )
    const char* label = reinterpret_cast<const char*>( inLabel );
    ImGui::PushID( label );
    const float width = HavanaUtils::Label( label );
    const ImVec2 box( width, ImGui::GetTextLineHeight() * static_cast<float>( inLines < 2 ? 2 : inLines ) + ImGui::GetStyle().FramePadding.y * 2.f );
    const bool changed = ImGui::InputTextMultiline( "##text", reinterpret_cast<char*>( inOutUtf8 ), static_cast<size_t>( inSize ), box );
    ImGui::PopID();
    return changed;
#else
    return false;
#endif
}


static bool Eng_ImGui_Vec2( const uint8_t* inLabel, Vector2* inOutValue )
{
#if USING( ME_EDITOR )
    return HavanaUtils::EditableVector( reinterpret_cast<const char*>( inLabel ), *inOutValue );
#else
    return false;
#endif
}


static bool Eng_ImGui_Vec4( const uint8_t* inLabel, float* inOutXyzw )
{
#if USING( ME_EDITOR )
    const char* label = reinterpret_cast<const char*>( inLabel );
    ImGui::PushID( label );
    HavanaUtils::Label( label );
    const bool changed = HavanaUtils::DragAxes( "##axes", inOutXyzw, 4, 0.1f, "%.3f" );
    ImGui::PopID();
    return changed;
#else
    return false;
#endif
}


static bool Eng_ImGui_Color( const uint8_t* inLabel, float* inOutRgba )
{
#if USING( ME_EDITOR )
    const char* label = reinterpret_cast<const char*>( inLabel );
    ImGui::PushID( label );
    HavanaUtils::Label( label );
    const bool changed = ImGui::ColorEdit4( "##color", inOutRgba, ImGuiColorEditFlags_Float );
    ImGui::PopID();
    return changed;
#else
    return false;
#endif
}


static bool Eng_ImGui_Enum( const uint8_t* inLabel, int* inOutIndex, const uint8_t* inNames )
{
#if USING( ME_EDITOR )
    const char* label = reinterpret_cast<const char*>( inLabel );
    // The choice names arrive newline-joined (JSON-free ABI crossing).
    std::vector<std::string> names;
    {
        const char* text = reinterpret_cast<const char*>( inNames );
        const char* start = text;
        for( const char* c = text;; ++c )
        {
            if( *c == '\n' || *c == '\0' )
            {
                names.emplace_back( start, c );
                start = c + 1;
                if( *c == '\0' )
                {
                    break;
                }
            }
        }
    }
    bool changed = false;
    ImGui::PushID( label );
    HavanaUtils::Label( label );
    const bool known = *inOutIndex >= 0 && *inOutIndex < static_cast<int>( names.size() );
    if( ImGui::BeginCombo( "##enum", known ? names[*inOutIndex].c_str() : "?" ) )
    {
        for( int i = 0; i < static_cast<int>( names.size() ); ++i )
        {
            if( ImGui::Selectable( names[i].c_str(), i == *inOutIndex ) && i != *inOutIndex )
            {
                *inOutIndex = i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
#else
    return false;
#endif
}


static bool Eng_ImGui_Double( const uint8_t* inLabel, double* inOutValue )
{
#if USING( ME_EDITOR )
    return HavanaUtils::Double( reinterpret_cast<const char*>( inLabel ), *inOutValue );
#else
    return false;
#endif
}


static bool Eng_ImGui_SliderFloat( const uint8_t* inLabel, float* inOutValue, float inMin, float inMax )
{
#if USING( ME_EDITOR )
    const char* label = reinterpret_cast<const char*>( inLabel );
    ImGui::PushID( label );
    HavanaUtils::Label( label );
    const bool changed = ImGui::SliderFloat( "##slider", inOutValue, inMin, inMax );
    ImGui::PopID();
    return changed;
#else
    return false;
#endif
}


static bool Eng_ImGui_SliderInt( const uint8_t* inLabel, int* inOutValue, int inMin, int inMax )
{
#if USING( ME_EDITOR )
    const char* label = reinterpret_cast<const char*>( inLabel );
    ImGui::PushID( label );
    HavanaUtils::Label( label );
    const bool changed = ImGui::SliderInt( "##slider", inOutValue, inMin, inMax );
    ImGui::PopID();
    return changed;
#else
    return false;
#endif
}


static void Eng_ImGui_Header( const uint8_t* inText )
{
    ImGui::SeparatorText( reinterpret_cast<const char*>( inText ) );
}


static void Eng_ImGui_Space( float inHeight )
{
    ImGui::Dummy( ImVec2( 0.f, inHeight > 0.f ? inHeight : ImGui::GetTextLineHeight() * 0.5f ) );
}


static void Eng_ImGui_Tooltip( const uint8_t* inText )
{
    if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
    {
        ImGui::SetTooltip( "%s", reinterpret_cast<const char*>( inText ) );
    }
}


static void Eng_ImGui_BeginDisabled()
{
    ImGui::BeginDisabled();
}


static void Eng_ImGui_EndDisabled()
{
    ImGui::EndDisabled();
}


static bool Eng_ImGui_TreeNode( const uint8_t* inLabel )
{
    return ImGui::TreeNodeEx( reinterpret_cast<const char*>( inLabel ), ImGuiTreeNodeFlags_SpanAvailWidth );
}


static void Eng_ImGui_TreePop()
{
    ImGui::TreePop();
}


static bool Eng_ImGui_SmallButton( const uint8_t* inLabel )
{
    return ImGui::SmallButton( reinterpret_cast<const char*>( inLabel ) );
}


static void Eng_ImGui_SameLine()
{
    ImGui::SameLine();
}


static bool Eng_ImGui_EntityRef( const uint8_t* inLabel, uint8_t* inOutGuid, int inSize )
{
#if USING( ME_EDITOR )
    const char* label = reinterpret_cast<const char*>( inLabel );
    uint64_t guid = std::strtoull( reinterpret_cast<const char*>( inOutGuid ), nullptr, 16 );
    bool changed = false;
    if( ScriptInspectorRows::EntityRow )
    {
        changed = ScriptInspectorRows::EntityRow( label, guid );
    }
    else
    {
        // Plain fallback without the editor hooks: show the target and allow clearing.
        ImGui::PushID( label );
        HavanaUtils::Label( label );
        std::string name = "None";
        if( guid != 0 )
        {
            auto world = ScriptBindings::GetWorld().lock();
            EntityHandle target = world ? world->FindEntityByGUID( guid ) : EntityHandle();
            name = target ? target->GetName() : "(missing)";
        }
        ImGui::TextUnformatted( name.c_str() );
        ImGui::SameLine();
        if( ImGui::SmallButton( "x" ) && guid != 0 )
        {
            guid = 0;
            changed = true;
        }
        ImGui::PopID();
    }
    if( changed )
    {
        if( guid == 0 )
        {
            inOutGuid[0] = 0;
        }
        else
        {
            std::snprintf( reinterpret_cast<char*>( inOutGuid ), static_cast<size_t>( inSize ), "%016llx", static_cast<unsigned long long>( guid ) );
        }
    }
    return changed;
#else
    return false;
#endif
}


static bool Eng_ImGui_AssetRef( const uint8_t* inLabel, uint8_t* inOutPath, int inSize, const uint8_t* inExtensions )
{
#if USING( ME_EDITOR )
    const char* label = reinterpret_cast<const char*>( inLabel );
    if( ScriptInspectorRows::AssetRow )
    {
        std::string path( reinterpret_cast<const char*>( inOutPath ) );
        if( ScriptInspectorRows::AssetRow( label, path, reinterpret_cast<const char*>( inExtensions ) ) )
        {
            std::strncpy( reinterpret_cast<char*>( inOutPath ), path.c_str(), static_cast<size_t>( inSize ) - 1 );
            inOutPath[inSize - 1] = 0;
            return true;
        }
        return false;
    }
    ImGui::PushID( label );
    HavanaUtils::Label( label );
    const bool changed = ImGui::InputText( "##path", reinterpret_cast<char*>( inOutPath ), static_cast<size_t>( inSize ) );
    ImGui::PopID();
    return changed;
#else
    return false;
#endif
}


void Register_ImGuiBindings( ScriptEngineAPI& inAPI )
{
    inAPI.ImGui_String        = Eng_ImGui_String;
    inAPI.ImGui_Multiline     = Eng_ImGui_Multiline;
    inAPI.ImGui_Vec2          = Eng_ImGui_Vec2;
    inAPI.ImGui_Vec4          = Eng_ImGui_Vec4;
    inAPI.ImGui_Color         = Eng_ImGui_Color;
    inAPI.ImGui_Enum          = Eng_ImGui_Enum;
    inAPI.ImGui_Double        = Eng_ImGui_Double;
    inAPI.ImGui_SliderFloat   = Eng_ImGui_SliderFloat;
    inAPI.ImGui_SliderInt     = Eng_ImGui_SliderInt;
    inAPI.ImGui_Header        = Eng_ImGui_Header;
    inAPI.ImGui_Space         = Eng_ImGui_Space;
    inAPI.ImGui_Tooltip       = Eng_ImGui_Tooltip;
    inAPI.ImGui_BeginDisabled = Eng_ImGui_BeginDisabled;
    inAPI.ImGui_EndDisabled   = Eng_ImGui_EndDisabled;
    inAPI.ImGui_TreeNode      = Eng_ImGui_TreeNode;
    inAPI.ImGui_TreePop       = Eng_ImGui_TreePop;
    inAPI.ImGui_SmallButton   = Eng_ImGui_SmallButton;
    inAPI.ImGui_SameLine      = Eng_ImGui_SameLine;
    inAPI.ImGui_EntityRef     = Eng_ImGui_EntityRef;
    inAPI.ImGui_AssetRef      = Eng_ImGui_AssetRef;
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
