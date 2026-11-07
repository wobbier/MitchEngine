#include "ReflectionUI.h"

#if USING( ME_EDITOR )

#include "ECS/EntityHandle.h"
#include "ECS/Entity.h"
#include "Components/Transform.h"
#include "EditorOperations.h"
#include "Events/EditorEvents.h"
#include "Materials/ShaderGraphParams.h"
#include "Math/Quaternion.h"
#include "Types/AssetDescriptor.h"
#include "Types/AssetType.h"
#include "Utils/CommonUtils.h"
#include "Selection.h"
#include <Utils/HavanaUtils.h>
#include <Widgets/ItemLog.h>
#include <Widgets/ScrubField.h>
#include <ImGuiTheme.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <unordered_map>

namespace ReflectionUI
{
    namespace
    {
        using namespace Reflection;

        constexpr float kRadToDeg = 180.f / 3.14159265f;
        constexpr const char* kEntityPayload = "DND_CHILD_TRANSFORM";

        struct Decorator
        {
            std::vector<std::string> Edits;
            FieldDecorator Draw;
            DecoratorFixUp FixUp;
        };

        // "Type.Field" -> its decorators, in registration order.
        std::map<std::string, std::vector<Decorator>>& Decorators()
        {
            static std::map<std::string, std::vector<Decorator>> decorators;
            return decorators;
        }

        std::map<std::string, CustomDrawer>& Drawers()
        {
            static std::map<std::string, CustomDrawer> drawers;
            return drawers;
        }

        // Euler angles being edited, so dragging doesn't fight quaternion round-trips.
        std::unordered_map<ImGuiID, Vector3>& EulerCache()
        {
            static std::unordered_map<ImGuiID, Vector3> cache;
            return cache;
        }

        std::unordered_map<ImGuiID, std::string>& PickerResults()
        {
            static std::unordered_map<ImGuiID, std::string> results;
            return results;
        }

        AssetType AssetTypeFromFilter( const std::string& InFilter )
        {
            for( unsigned int i = 0; i < static_cast<unsigned int>( AssetType::Count ); ++i )
            {
                const AssetType type = static_cast<AssetType>( i );
                std::string name = AssetTypeToString( type );
                if( !name.empty() && std::equal( name.begin(), name.end(), InFilter.begin(), InFilter.end(), []( char a, char b ) { return std::tolower( a ) == std::tolower( b ); } ) )
                {
                    return type;
                }
            }
            return AssetType::Unknown;
        }

        void Tooltip( const FieldInfo* InField, const std::string& InHint = std::string() )
        {
            if( !ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) || ImGui::IsItemActive() )
            {
                return;
            }
            const std::string text = InField && !InField->Tooltip.empty() ? InField->Tooltip + ( InHint.empty() ? "" : "\n\n" + InHint ) : InHint;
            if( !text.empty() )
            {
                ImGui::SetTooltip( "%s", text.c_str() );
            }
        }

        // The default value of the top-level field being drawn (from Context::Defaults).
        const json*& CurrentDefault()
        {
            static const json* current = nullptr;
            return current;
        }

        // Numbers drawn as unit fields (UX-2): floats, doubles and integers without named
        // choices, unless the field asks for a classic slider.
        bool IsUnitField( const FieldInfo* InField, const ValueOps& InOps )
        {
            if( InField && InField->Widget == Reflection::WidgetKind::Slider )
            {
                return false;
            }
            switch( InOps.Type )
            {
            case PropertyType::Float:
            case PropertyType::Double:
                return true;
            case PropertyType::Int:
            case PropertyType::UInt:
                // 64-bit integers keep the exact DragScalar: a double can't hold every value.
                return !( InField && InField->ChoiceName && InField->ChoiceCount > 0 ) && InOps.IntBytes < 8;
            default:
                return false;
            }
        }

        double ReadNumber( const ValueOps& InOps, const void* InValue )
        {
            switch( InOps.Type )
            {
            case PropertyType::Float: return *static_cast<const float*>( InValue );
            case PropertyType::Double: return *static_cast<const double*>( InValue );
            default:
            {
                const size_t bytes = std::min<size_t>( InOps.IntBytes, sizeof( int64_t ) );
                uint64_t raw = 0;
                std::memcpy( &raw, InValue, bytes );
                if( InOps.Type == PropertyType::UInt )
                {
                    return static_cast<double>( raw );
                }
                if( bytes < 8 && ( raw >> ( bytes * 8 - 1 ) ) & 1 )
                {
                    raw |= ~( ( uint64_t( 1 ) << ( bytes * 8 ) ) - 1 );
                }
                return static_cast<double>( static_cast<int64_t>( raw ) );
            }
            }
        }

        void WriteNumber( const ValueOps& InOps, void* InValue, double InNumber )
        {
            switch( InOps.Type )
            {
            case PropertyType::Float: *static_cast<float*>( InValue ) = static_cast<float>( InNumber ); return;
            case PropertyType::Double: *static_cast<double*>( InValue ) = InNumber; return;
            default:
            {
                const size_t bytes = std::min<size_t>( InOps.IntBytes, sizeof( int64_t ) );
                const int64_t whole = static_cast<int64_t>( std::llround( InNumber ) );
                std::memcpy( InValue, &whole, bytes );
                return;
            }
            }
        }

        MeWidgets::NumberSpec SpecFor( const FieldInfo* InField, const ValueOps& InOps )
        {
            MeWidgets::NumberSpec spec;
            const bool integer = InOps.Type == PropertyType::Int || InOps.Type == PropertyType::UInt;
            spec.Integer = integer;
            if( InField )
            {
                spec.FieldUnit = InField->FieldUnit;
                if( spec.FieldUnit == Reflection::Unit::None && InField->Has( FieldFlags::Angle ) )
                {
                    spec.FieldUnit = Reflection::Unit::Radians;
                }
                spec.HasRange = InField->HasRange;
                spec.Min = InField->Min;
                spec.Max = InField->Max;
                spec.HasSoftRange = InField->HasSoftRange;
                spec.SoftMin = InField->SoftMin;
                spec.SoftMax = InField->SoftMax;
                spec.Log = InField->LogScale;
                spec.Speed = InField->Speed;
            }
            // The 0.1 default speed suits metres; radians and whole numbers want their own.
            const bool defaultSpeed = !InField || InField->Speed == 0.1f;
            if( defaultSpeed && spec.FieldUnit == Reflection::Unit::Radians )
            {
                spec.Speed = 0.5 * 3.14159265358979 / 180.0;
            }
            if( defaultSpeed && integer )
            {
                spec.Speed = 0.25;
            }
            if( InField && InField->Has( FieldFlags::Drag ) && spec.HasRange )
            {
                // .Drag( speed ): the field scrubs at its speed; the range only clamps.
                spec.HasRange = false;
                spec.HasClamp = true;
                spec.ClampMin = InField->Min;
                spec.ClampMax = InField->Max;
            }
            if( integer && !spec.HasRange )
            {
                // Whole numbers stay inside their type (a clamp: the scrub keeps its speed).
                const int bits = static_cast<int>( std::min<size_t>( InOps.IntBytes, 4 ) * 8 );
                spec.HasClamp = true;
                if( InOps.Type == PropertyType::UInt )
                {
                    spec.ClampMin = 0.0;
                    spec.ClampMax = std::ldexp( 1.0, bits ) - 1.0;
                }
                else
                {
                    spec.ClampMin = -std::ldexp( 1.0, bits - 1 );
                    spec.ClampMax = std::ldexp( 1.0, bits - 1 ) - 1.0;
                }
            }
            if( InField && InField->DefaultCount > 0 )
            {
                spec.HasDefault = true;
                spec.Default = InField->DefaultValue[0];
            }
            else if( const json* fallback = CurrentDefault(); fallback && fallback->is_number() )
            {
                spec.HasDefault = true;
                spec.Default = fallback->get<double>();
            }
            return spec;
        }

        std::string ScrubHint( const MeWidgets::NumberSpec& InSpec )
        {
            std::string hint = "Drag the label or the value to change it (Shift fine, Ctrl coarse, Alt snaps); click the value to type one";
            if( InSpec.HasDefault )
            {
                hint += "\nDouble-click the label to reset to " + MeWidgets::FormatValue( InSpec.FieldUnit, InSpec.Default, InSpec.Integer ? 0 : -1 );
            }
            return hint;
        }

        bool DrawValue( const char* InLabel, const FieldInfo* InField, const ValueOps& InOps, void* InValue, bool InMixed );

        // Tells editor automation what a scalar field's widget shows (imgui-assert-value).
        void ReportValue( const FieldInfo* InField, const ValueOps& InOps, const void* InValue )
        {
            if( !MeWidgets::ItemLog::IsEnabled() )
            {
                return;
            }
            // Widgets that show something other than the stored number report it themselves.
            if( IsUnitField( InField, InOps ) || ( InField && ( InField->Has( FieldFlags::Asset ) || ( InField->ChoiceName && InField->ChoiceCount > 0 ) ) ) )
            {
                return;
            }
            if( InField && InField->Has( FieldFlags::Angle ) && InOps.Type == PropertyType::Float )
            {
                char degrees[64];
                std::snprintf( degrees, sizeof( degrees ), "%.1f", *static_cast<const float*>( InValue ) * kRadToDeg );
                MeWidgets::ItemLog::SetLastItemValue( degrees );
                return;
            }
            char text[64] = {};
            switch( InOps.Type )
            {
            case PropertyType::Bool:
                MeWidgets::ItemLog::SetLastItemValue( *static_cast<const bool*>( InValue ) ? "true" : "false" );
                return;
            case PropertyType::Float:
                std::snprintf( text, sizeof( text ), "%.6g", *static_cast<const float*>( InValue ) );
                break;
            case PropertyType::Double:
                std::snprintf( text, sizeof( text ), "%.9g", *static_cast<const double*>( InValue ) );
                break;
            case PropertyType::Int:
            case PropertyType::UInt:
            {
                int64_t value = 0;
                std::memcpy( &value, InValue, std::min<size_t>( InOps.IntBytes, sizeof( value ) ) );
                if( InOps.Type == PropertyType::Int && InOps.IntBytes < sizeof( value ) && ( value >> ( InOps.IntBytes * 8 - 1 ) ) & 1 )
                {
                    value |= ~( ( int64_t( 1 ) << ( InOps.IntBytes * 8 ) ) - 1 );
                }
                std::snprintf( text, sizeof( text ), "%lld", static_cast<long long>( value ) );
                break;
            }
            case PropertyType::String:
                MeWidgets::ItemLog::SetLastItemValue( *static_cast<const std::string*>( InValue ) );
                return;
            case PropertyType::Enum:
            {
                const EnumInfo* info = InOps.GetEnumInfo();
                const int64_t current = InOps.GetEnum( InValue );
                const char* name = info ? info->ToName( current ) : nullptr;
                MeWidgets::ItemLog::SetLastItemValue( name ? std::string( name ) : std::to_string( current ) );
                return;
            }
            default:
                return;
            }
            MeWidgets::ItemLog::SetLastItemValue( text );
        }

        bool DrawFloat( const char* InLabel, const FieldInfo* InField, float& InOutValue )
        {
            const float speed = InField ? InField->Speed : 0.1f;
            if( InField && InField->Has( FieldFlags::Angle ) )
            {
                float degrees = InOutValue * kRadToDeg;
                const bool changed = InField->HasRange
                    ? ImGui::SliderFloat( InLabel, &degrees, InField->Min * kRadToDeg, InField->Max * kRadToDeg, "%.1f deg" )
                    : ImGui::DragFloat( InLabel, &degrees, 0.5f, 0.f, 0.f, "%.1f deg" );
                if( changed )
                {
                    InOutValue = degrees / kRadToDeg;
                }
                return changed;
            }
            if( InField && InField->Has( FieldFlags::Drag ) )
            {
                return ImGui::DragFloat( InLabel, &InOutValue, speed, InField->HasRange ? InField->Min : 0.f,
                    InField->HasRange ? InField->Max : 0.f, "%.3f", InField->HasRange ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None );
            }
            if( InField && InField->HasRange )
            {
                return ImGui::SliderFloat( InLabel, &InOutValue, InField->Min, InField->Max );
            }
            return ImGui::DragFloat( InLabel, &InOutValue, speed, 0.f, 0.f, "%.3f" );
        }

        // Integer fields with named choices: a combo, or a multi-select mask.
        bool DrawChoices( const char* InLabel, const FieldInfo& InField, const ValueOps& InOps, void* InValue )
        {
            const size_t bytes = std::min<size_t>( InOps.IntBytes, sizeof( uint64_t ) );
            uint64_t value = 0;
            std::memcpy( &value, InValue, bytes );
            const uint64_t valueMask = bytes >= 8 ? ~0ull : ( ( 1ull << ( bytes * 8 ) ) - 1 );
            const uint64_t allChoices = InField.ChoiceCount >= 64 ? ~0ull : ( ( 1ull << InField.ChoiceCount ) - 1 );
            bool changed = false;

            std::string preview;
            if( InField.ChoiceMask )
            {
                const uint64_t chosen = value & allChoices;
                int count = 0;
                int last = 0;
                for( int i = 0; i < InField.ChoiceCount; ++i )
                {
                    if( chosen & ( 1ull << i ) )
                    {
                        ++count;
                        last = i;
                    }
                }
                preview = chosen == allChoices ? "Everything" : count == 0 ? "Nothing" : count == 1 ? InField.ChoiceName( last ) : "Mixed (" + std::to_string( count ) + ")";
            }
            else
            {
                preview = static_cast<int64_t>( value ) >= 0 && static_cast<int>( value ) < InField.ChoiceCount ? InField.ChoiceName( static_cast<int>( value ) ) : std::to_string( value );
            }

            const bool open = ImGui::BeginCombo( InLabel, preview.c_str() );
            MeWidgets::ItemLog::SetLastItemValue( preview );
            if( open )
            {
                if( InField.ChoiceMask )
                {
                    if( ImGui::Selectable( "Everything", false, ImGuiSelectableFlags_DontClosePopups ) )
                    {
                        value = ( value & ~allChoices ) | allChoices;
                        changed = true;
                    }
                    if( ImGui::Selectable( "Nothing", false, ImGuiSelectableFlags_DontClosePopups ) )
                    {
                        value &= ~allChoices;
                        changed = true;
                    }
                    ImGui::Separator();
                }
                for( int i = 0; i < InField.ChoiceCount; ++i )
                {
                    const std::string name = InField.ChoiceName( i );
                    if( name.empty() )
                    {
                        continue;   // an unused slot (e.g. agent types not defined)
                    }
                    ImGui::PushID( i );
                    if( InField.ChoiceMask )
                    {
                        bool selected = ( value & ( 1ull << i ) ) != 0;
                        if( ImGui::Checkbox( name.c_str(), &selected ) )
                        {
                            value = selected ? ( value | ( 1ull << i ) ) : ( value & ~( 1ull << i ) );
                            changed = true;
                        }
                    }
                    else if( ImGui::Selectable( name.c_str(), value == static_cast<uint64_t>( i ) ) && value != static_cast<uint64_t>( i ) )
                    {
                        value = static_cast<uint64_t>( i );
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if( changed )
            {
                value &= valueMask;
                std::memcpy( InValue, &value, bytes );
            }
            return changed;
        }

        bool DrawInt( const char* InLabel, const FieldInfo* InField, const ValueOps& InOps, void* InValue )
        {
            if( InField && InField->ChoiceName && InField->ChoiceCount > 0 )
            {
                return DrawChoices( InLabel, *InField, InOps, InValue );
            }
            ImGuiDataType type = ImGuiDataType_S32;
            const bool isSigned = InOps.Type == PropertyType::Int;
            switch( InOps.IntBytes )
            {
            case 1: type = isSigned ? ImGuiDataType_S8 : ImGuiDataType_U8; break;
            case 2: type = isSigned ? ImGuiDataType_S16 : ImGuiDataType_U16; break;
            case 8: type = isSigned ? ImGuiDataType_S64 : ImGuiDataType_U64; break;
            default: type = isSigned ? ImGuiDataType_S32 : ImGuiDataType_U32; break;
            }
            if( InField && InField->HasRange && InOps.IntBytes <= 4 )
            {
                int64_t value = 0;
                std::memcpy( &value, InValue, InOps.IntBytes );
                if( isSigned && InOps.IntBytes < 8 && ( value >> ( InOps.IntBytes * 8 - 1 ) ) & 1 )
                {
                    value |= ~( ( int64_t( 1 ) << ( InOps.IntBytes * 8 ) ) - 1 );
                }
                int asInt = static_cast<int>( value );
                if( ImGui::SliderInt( InLabel, &asInt, static_cast<int>( InField->Min ), static_cast<int>( InField->Max ) ) )
                {
                    value = asInt;
                    std::memcpy( InValue, &value, InOps.IntBytes );
                    return true;
                }
                return false;
            }
            return ImGui::DragScalar( InLabel, type, InValue, 0.25f );
        }

        bool DrawAssetPath( const char* InLabel, const FieldInfo* InField, std::string& InOutPath )
        {
            bool changed = false;
            ImGui::PushID( InLabel );
            const float gap = ImGuiTheme::InnerGap();
            ImGui::SetNextItemWidth( std::max( ImGui::CalcItemWidth() - ImGuiTheme::IconButtonsWidth( 2 ), ImGuiTheme::Em( 2.f ) ) );
            changed |= ImGui::InputTextWithHint( "##path", "None", &InOutPath, ImGuiInputTextFlags_EnterReturnsTrue );
            MeWidgets::ItemLog::SetLastItemValue( InOutPath );
            if( ImGui::BeginDragDropTarget() )
            {
                if( ImGui::AcceptDragDropPayload( AssetDescriptor::kDragAndDropPayload ) )
                {
                    if( AssetDescriptor* descriptor = AssetDescriptor::GetDragged() )
                    {
                        InOutPath = descriptor->FullPath.GetLocalPathString();
                        changed = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine( 0.f, gap );
            // The picker answers asynchronously (a later frame); results are keyed by widget ID.
            const ImGuiID pickerId = ImGui::GetID( "picker" );
            if( ImGui::Button( "...", ImGuiTheme::IconButtonSize() ) )
            {
                const AssetType filter = InField ? AssetTypeFromFilter( InField->AssetFilter ) : AssetType::Unknown;
                RequestAssetSelectionEvent evt( [pickerId]( Path selected ) {
                    PickerResults()[pickerId] = selected.GetLocalPathString();
                }, filter );
                evt.Fire();
            }
            ImGui::SetItemTooltip( "Pick an asset" );
            ImGui::SameLine( 0.f, gap );
            ImGui::BeginDisabled( InOutPath.empty() );
            if( ImGui::Button( "x", ImGuiTheme::IconButtonSize() ) )
            {
                InOutPath.clear();
                changed = true;
            }
            ImGui::SetItemTooltip( "Clear" );
            ImGui::EndDisabled();
            auto result = PickerResults().find( pickerId );
            if( result != PickerResults().end() )
            {
                InOutPath = result->second;
                PickerResults().erase( result );
                changed = true;
            }
            ImGui::PopID();
            return changed;
        }

        // A vector axis' display format with the field's unit ("%.3f m", "%.2f°"). Units that
        // convert between stored and shown values (radians, gain, percent) keep the plain format.
        std::string AxisFormat( const FieldInfo* InField, const char* InBase )
        {
            if( !InField )
            {
                return InBase;
            }
            switch( InField->FieldUnit )
            {
            case Reflection::Unit::None:
            case Reflection::Unit::Radians:
            case Reflection::Unit::LinearGain:
            case Reflection::Unit::Percent:
            case Reflection::Unit::PitchRatio:
            case Reflection::Unit::FStop:
                return InBase;
            case Reflection::Unit::Degrees:
            case Reflection::Unit::DegreesPerSecond:
                return std::string( InBase ) + MeWidgets::GetUnitInfo( InField->FieldUnit ).Symbol;
            default:
                return std::string( InBase ) + " " + MeWidgets::GetUnitInfo( InField->FieldUnit ).Symbol;
            }
        }

        bool DrawVector( const char* InLabel, const FieldInfo* InField, const ValueOps& InOps, void* InValue )
        {
            const std::string axisFormat = AxisFormat( InField, "%.3f" );
            const bool isColor = InField && InField->Has( FieldFlags::Color );
            const ImGuiColorEditFlags colorFlags = ImGuiColorEditFlags_Float | ( InField && InField->Has( FieldFlags::HDR ) ? ImGuiColorEditFlags_HDR : 0 );
            const float speed = InField ? InField->Speed : 0.1f;
            switch( InOps.Type )
            {
            case PropertyType::Vector2:
                return HavanaUtils::DragAxes( InLabel, &static_cast<Vector2*>( InValue )->x, 2, speed, axisFormat.c_str() );
            case PropertyType::Vector3:
            {
                Vector3& value = *static_cast<Vector3*>( InValue );
                if( isColor )
                {
                    return ImGui::ColorEdit3( InLabel, &value.x, colorFlags );
                }
                return HavanaUtils::DragAxes( InLabel, &value.x, 3, speed, axisFormat.c_str() );
            }
            case PropertyType::Vector4:
            {
                Vector4& value = *static_cast<Vector4*>( InValue );
                if( isColor )
                {
                    return ImGui::ColorEdit4( InLabel, &value.x, colorFlags | ImGuiColorEditFlags_AlphaBar );
                }
                return HavanaUtils::DragAxes( InLabel, &value.x, 4, speed, axisFormat.c_str() );
            }
            case PropertyType::Quaternion:
            {
                Quaternion& value = *static_cast<Quaternion*>( InValue );
                const ImGuiID id = ImGui::GetID( InLabel );
                auto& cache = EulerCache();
                Vector3 euler = cache.count( id ) ? cache[id] : Quaternion::ToEulerAngles( value );
                bool active = false;
                const bool changed = HavanaUtils::DragAxes( InLabel, &euler.x, 3, 0.5f, "%.2f\xC2\xB0", &active );
                if( active )
                {
                    cache[id] = euler;
                }
                else
                {
                    cache.erase( id );
                }
                if( changed )
                {
                    value = Quaternion::FromEulerDegrees( euler );
                }
                return changed;
            }
            default:
                return false;
            }
        }

        bool DrawArray( const char* InLabel, const FieldInfo* InField, const ValueOps& InOps, void* InValue )
        {
            bool changed = false;
            const size_t size = InOps.ArraySize( InValue );
            const float gap = ImGuiTheme::InnerGap();
            // AllowOverlap: otherwise the full-width node takes the clicks meant for + and -.
            // Asset lists (a few paths) start open so their entries are in view.
            const ImGuiTreeNodeFlags openFlag = InField && InField->Has( FieldFlags::Asset ) ? ImGuiTreeNodeFlags_DefaultOpen : 0;
            const bool open = ImGui::TreeNodeEx( InLabel, ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | openFlag, "%s [%zu]", InLabel, size );
            Tooltip( InField );
            // + and - end the row where the values end.
            ImGui::SameLine();
            ImGui::SetCursorScreenPos( ImVec2( HavanaUtils::RowRight() - ImGuiTheme::IconButtonsWidth( 2 ) + gap, ImGui::GetCursorScreenPos().y ) );
            ImGui::PushID( InLabel );
            if( ImGui::Button( "+", ImGuiTheme::IconButtonSize() ) )
            {
                InOps.ArrayResize( InValue, size + 1 );
                changed = true;
                // Show the new element.
                ImGui::GetStateStorage()->SetInt( ImGui::GetID( InLabel ), 1 );
            }
            ImGui::SetItemTooltip( "Add an element" );
            ImGui::SameLine( 0.f, gap );
            ImGui::BeginDisabled( size == 0 );
            if( ImGui::Button( "-", ImGuiTheme::IconButtonSize() ) )
            {
                InOps.ArrayResize( InValue, size - 1 );
                changed = true;
            }
            ImGui::SetItemTooltip( "Remove the last element" );
            ImGui::EndDisabled();
            ImGui::PopID();
            if( open )
            {
                // Element moves and removals go through the array's JSON (ValueOps has no erase).
                enum class Edit { None, Remove, Up, Down } edit = Edit::None;
                size_t editIndex = 0;
                const size_t count = InOps.ArraySize( InValue );
                for( size_t i = 0; i < count; ++i )
                {
                    ImGui::PushID( static_cast<int>( i ) );
                    const std::string elementLabel = "[" + std::to_string( i ) + "]";
                    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
                    // Remove button at the end of the label column (the index label is short).
                    const float removeX = HavanaUtils::ValueColumnX() - gap - ImGui::GetFrameHeight();
                    ImGui::SetCursorScreenPos( ImVec2( removeX, rowStart.y ) );
                    if( ImGui::Button( "-", ImGuiTheme::IconButtonSize() ) )
                    {
                        edit = Edit::Remove;
                        editIndex = i;
                    }
                    ImGui::SetItemTooltip( "Remove this element (right-click the index to reorder)" );
                    ImGui::SetCursorScreenPos( rowStart );
                    // Asset lists (std::vector<Path>) pick each element from their filter.
                    const FieldInfo* elementField = InField && InField->Has( FieldFlags::Asset ) ? InField : nullptr;
                    changed |= DrawValue( elementLabel.c_str(), elementField, *InOps.Element, InOps.ArrayAt( InValue, i ), false );
                    if( ImGui::IsMouseHoveringRect( rowStart, ImVec2( removeX, rowStart.y + ImGui::GetFrameHeight() ) ) && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
                    {
                        ImGui::OpenPopup( "ElementContext" );
                    }
                    if( ImGui::BeginPopup( "ElementContext" ) )
                    {
                        if( ImGui::MenuItem( "Move Up", nullptr, false, i > 0 ) )
                        {
                            edit = Edit::Up;
                            editIndex = i;
                        }
                        if( ImGui::MenuItem( "Move Down", nullptr, false, i + 1 < count ) )
                        {
                            edit = Edit::Down;
                            editIndex = i;
                        }
                        ImGui::Separator();
                        if( ImGui::MenuItem( "Remove" ) )
                        {
                            edit = Edit::Remove;
                            editIndex = i;
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                if( edit != Edit::None )
                {
                    json elements;
                    ValueToJson( InOps, InValue, elements );
                    if( elements.is_array() && editIndex < elements.size() )
                    {
                        if( edit == Edit::Remove )
                        {
                            elements.erase( elements.begin() + static_cast<std::ptrdiff_t>( editIndex ) );
                        }
                        else
                        {
                            const size_t other = edit == Edit::Up ? editIndex - 1 : editIndex + 1;
                            std::swap( elements[editIndex], elements[other] );
                        }
                        ValueFromJson( InOps, InValue, elements );
                        changed = true;
                    }
                }
                ImGui::TreePop();
            }
            return changed;
        }

        bool DrawFieldsOf( const TypeInfo& InType, void* InInstance )
        {
            Context nested;
            // A struct field's defaults are the struct's own fields.
            if( CurrentDefault() && CurrentDefault()->is_object() )
            {
                nested.Defaults = CurrentDefault();
            }
            return DrawType( InType, InInstance, nested );
        }

        bool DrawValue( const char* InLabel, const FieldInfo* InField, const ValueOps& InOps, void* InValue, bool InMixed )
        {
            ImGui::PushID( InLabel );
            bool changed = false;

            // Structs and arrays get a tree node instead of a label column.
            if( InOps.Type == PropertyType::Struct )
            {
                if( ImGui::TreeNodeEx( InLabel, ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding ) )
                {
                    changed = DrawFieldsOf( InOps.GetStructType(), InValue );
                    ImGui::TreePop();
                }
                ImGui::PopID();
                return changed;
            }
            if( InOps.Type == PropertyType::Array )
            {
                changed = DrawArray( InLabel, InField, InOps, InValue );
                ImGui::PopID();
                return changed;
            }

            if( InMixed )
            {
                ImGui::PushStyleColor( ImGuiCol_Text, ImGuiTheme::WarningText() );
            }
            const float valueWidth = HavanaUtils::Label( InLabel );
            // Numbers scrub from their label too (UX-2), and reset on a double-click.
            const bool unitField = IsUnitField( InField, InOps );
            MeWidgets::NumberSpec spec;
            if( unitField )
            {
                spec = SpecFor( InField, InOps );
                double number = ReadNumber( InOps, InValue );
                if( MeWidgets::ScrubLastItem( number, spec, valueWidth, InMixed ) )
                {
                    WriteNumber( InOps, InValue, number );
                    changed = true;
                }
            }
            if( InMixed )
            {
                ImGui::PopStyleColor();
                if( ImGui::IsItemHovered() )
                {
                    ImGui::SetTooltip( "Selected entities have different values" );
                }
            }
            else
            {
                Tooltip( InField, unitField ? ScrubHint( spec ) : std::string() );
            }
            ImGui::SetNextItemWidth( valueWidth );
            ImGui::PushItemFlag( ImGuiItemFlags_MixedValue, InMixed );

            switch( InOps.Type )
            {
            case PropertyType::Bool:
                changed = ImGui::Checkbox( "##value", static_cast<bool*>( InValue ) );
                break;
            case PropertyType::Int:
            case PropertyType::UInt:
            case PropertyType::Float:
            case PropertyType::Double:
                if( unitField )
                {
                    double number = ReadNumber( InOps, InValue );
                    if( MeWidgets::UnitField( "##value", number, spec ) )
                    {
                        WriteNumber( InOps, InValue, number );
                        changed = true;
                    }
                }
                else if( InOps.Type == PropertyType::Float )
                {
                    changed |= DrawFloat( "##value", InField, *static_cast<float*>( InValue ) );
                }
                else if( InOps.Type == PropertyType::Double )
                {
                    changed |= ImGui::DragScalar( "##value", ImGuiDataType_Double, InValue, InField ? InField->Speed : 0.1f );
                }
                else
                {
                    changed |= DrawInt( "##value", InField, InOps, InValue );
                }
                break;
            case PropertyType::String:
            {
                std::string& text = *static_cast<std::string*>( InValue );
                if( InField && InField->Has( FieldFlags::Asset ) )
                {
                    changed = DrawAssetPath( "##value", InField, text );
                }
                else if( InField && InField->Has( FieldFlags::Multiline ) )
                {
                    changed = ImGui::InputTextMultiline( "##value", &text, ImVec2( ImGui::CalcItemWidth(), ImGui::GetTextLineHeight() * 4.f ) );
                }
                else
                {
                    ImGui::InputText( "##value", &text );
                    changed = ImGui::IsItemDeactivatedAfterEdit();
                }
                break;
            }
            case PropertyType::Vector2:
            case PropertyType::Vector3:
            case PropertyType::Vector4:
            case PropertyType::Quaternion:
                changed = DrawVector( "##value", InField, InOps, InValue );
                break;
            case PropertyType::Enum:
            {
                const EnumInfo* info = InOps.GetEnumInfo();
                const int64_t current = InOps.GetEnum( InValue );
                const char* currentName = info ? info->ToName( current ) : nullptr;
                const std::string preview = currentName ? currentName : std::to_string( current );
                if( ImGui::BeginCombo( "##value", preview.c_str() ) )
                {
                    if( info )
                    {
                        for( const auto& [name, value] : info->Values )
                        {
                            if( ImGui::Selectable( name.c_str(), value == current ) && value != current )
                            {
                                InOps.SetEnum( InValue, value );
                                changed = true;
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            case PropertyType::Custom:
            {
                auto drawer = Drawers().find( InOps.CppName );
                if( drawer != Drawers().end() )
                {
                    changed = drawer->second( "##value", InField, InValue );
                }
                else
                {
                    json value;
                    InOps.CustomToJson( InValue, value );
                    ImGui::TextDisabled( "%s", value.dump().c_str() );
                }
                break;
            }
            default:
                ImGui::TextDisabled( "<%s>", ToString( InOps.Type ) );
                break;
            }
            ReportValue( InField, InOps, InValue );

            ImGui::PopItemFlag();
            ImGui::PopID();
            return changed;
        }
    }


    bool FieldMatches( const Reflection::FieldInfo& InField, const std::string& InFilter )
    {
        auto lower = []( std::string InText ) {
            std::transform( InText.begin(), InText.end(), InText.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
            return InText;
        };
        return lower( InField.Name ).find( InFilter ) != std::string::npos
            || ( !InField.DisplayName.empty() && lower( InField.DisplayName ).find( InFilter ) != std::string::npos );
    }


    bool AnyFieldMatches( const Reflection::TypeInfo& InType, void* InInstance, const std::string& InFilter )
    {
        bool any = false;
        InType.ForEachField( InInstance, [&]( const Reflection::FieldInfo& field, void* ) {
            any = any || ( !field.Has( Reflection::FieldFlags::Hidden ) && !field.Has( Reflection::FieldFlags::CustomInspector ) && FieldMatches( field, InFilter ) );
        } );
        return any;
    }


    bool DrawType( const Reflection::TypeInfo& InType, void* InInstance, Context& InContext )
    {
        bool anyChanged = false;
        std::string currentCategory;
        bool categoryOpen = true;

        InType.ForEachField( InInstance, [&]( const Reflection::FieldInfo& field, void* owner ) {
            if( field.Has( Reflection::FieldFlags::Hidden )
                || ( field.Has( Reflection::FieldFlags::CustomInspector ) && InContext.Others.empty() )
                || ( !InContext.Filter.empty() && !FieldMatches( field, InContext.Filter ) ) )
            {
                return;
            }
            if( InContext.Drawn )
            {
                InContext.Drawn->push_back( field.Name );
            }
            if( field.Category != currentCategory )
            {
                if( !currentCategory.empty() && categoryOpen )
                {
                    ImGui::TreePop();
                }
                currentCategory = field.Category;
                categoryOpen = currentCategory.empty() || ImGui::TreeNodeEx( currentCategory.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding );
            }
            if( !categoryOpen )
            {
                return;
            }

            // Mixed when another selected instance holds a different value.
            bool mixed = false;
            json primaryValue;
            if( !InContext.Others.empty() )
            {
                primaryValue = Reflection::GetPathJson( InType, InInstance, field.Name );
                for( void* other : InContext.Others )
                {
                    if( Reflection::GetPathJson( InType, other, field.Name ) != primaryValue )
                    {
                        mixed = true;
                        break;
                    }
                }
            }

            const bool overridden = InContext.IsOverridden && InContext.IsOverridden( field.Name );
            const ImVec2 rowStart = ImGui::GetCursorScreenPos();

            const bool readOnly = field.Has( Reflection::FieldFlags::ReadOnly );
            ImGui::BeginDisabled( readOnly );
            if( overridden )
            {
                ImGui::PushStyleColor( ImGuiCol_Text, ImGuiTheme::AccentText() );
            }
            const json* previousDefault = CurrentDefault();
            CurrentDefault() = nullptr;
            if( InContext.Defaults && InContext.Defaults->is_object() )
            {
                const auto found = InContext.Defaults->find( field.Name );
                CurrentDefault() = found != InContext.Defaults->end() ? &*found : nullptr;
            }
            const bool changed = DrawValue( field.DisplayName.c_str(), &field, *field.Ops, field.Get( owner ), mixed );
            CurrentDefault() = previousDefault;
            if( overridden )
            {
                ImGui::PopStyleColor();
            }
            ImGui::EndDisabled();

            // Prefab override marker (in the indent, left of the label) and label context menu.
            const ImVec2 labelMax( HavanaUtils::ValueColumnX(), rowStart.y + ImGui::GetFrameHeight() );
            if( overridden )
            {
                const float markerRight = rowStart.x - ImGuiTheme::InnerGap() * 0.5f;
                const float markerWidth = std::max( 2.f, std::round( ImGui::GetFontSize() / 6.f ) );
                ImGui::GetWindowDrawList()->AddRectFilled( ImVec2( markerRight - markerWidth, rowStart.y + 1.f ), ImVec2( markerRight, labelMax.y - 1.f ), ImGui::GetColorU32( ImGuiTheme::AccentText() ) );
            }
            if( InContext.OnPrefabAction )
            {
                ImGui::PushID( field.Name.c_str() );
                if( ImGui::IsMouseHoveringRect( rowStart, labelMax ) && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
                {
                    ImGui::OpenPopup( "PrefabFieldContext" );
                }
                if( ImGui::BeginPopup( "PrefabFieldContext" ) )
                {
                    ImGui::TextDisabled( "%s", field.DisplayName.c_str() );
                    ImGui::Separator();
                    if( ImGui::MenuItem( "Revert to Prefab", nullptr, false, overridden ) )
                    {
                        InContext.OnPrefabAction( field.Name, false );
                    }
                    if( ImGui::MenuItem( "Apply to Prefab", nullptr, false, overridden ) )
                    {
                        InContext.OnPrefabAction( field.Name, true );
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }

            auto markChanged = [&]( const std::string& InField ) {
                anyChanged = true;
                if( std::find( InContext.ChangedFields.begin(), InContext.ChangedFields.end(), InField ) == InContext.ChangedFields.end() )
                {
                    InContext.ChangedFields.push_back( InField );
                }
                if( !InContext.Others.empty() )
                {
                    const json value = Reflection::GetPathJson( InType, InInstance, InField );
                    for( void* other : InContext.Others )
                    {
                        Reflection::SetPathJson( InType, other, InField, value );
                    }
                }
            };
            if( changed )
            {
                markChanged( field.Name );
            }

            // Decorators: extra rows under the field, edits reported like the field's own.
            const auto decorated = InContext.Filter.empty() ? Decorators().find( InType.Name + "." + field.Name ) : Decorators().end();
            if( decorated != Decorators().end() )
            {
                ImGui::PushID( field.Name.c_str() );
                ImGui::BeginDisabled( readOnly );
                for( size_t i = 0; i < decorated->second.size(); ++i )
                {
                    const Decorator& decorator = decorated->second[i];
                    // Snapshot the listed fields so only those the gesture changed are reported and
                    // copied to the other selected instances (a cone drag may leave Inner alone).
                    std::vector<json> before;
                    before.reserve( decorator.Edits.size() );
                    for( const std::string& edit : decorator.Edits )
                    {
                        before.push_back( Reflection::GetPathJson( InType, InInstance, edit ) );
                    }
                    ImGui::PushID( static_cast<int>( i ) );
                    const bool edited = decorator.Draw && decorator.Draw( InInstance, InType );
                    ImGui::PopID();
                    if( edited )
                    {
                        for( size_t e = 0; e < decorator.Edits.size(); ++e )
                        {
                            if( Reflection::GetPathJson( InType, InInstance, decorator.Edits[e] ) != before[e] )
                            {
                                markChanged( decorator.Edits[e] );
                            }
                        }
                        // Only the changed fields were copied: restore each other instance's
                        // invariants against the fields it kept (a capsule's Height >= 2 Radius).
                        if( decorator.FixUp )
                        {
                            for( void* other : InContext.Others )
                            {
                                decorator.FixUp( other, InType );
                            }
                        }
                    }
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        } );

        if( !currentCategory.empty() && categoryOpen )
        {
            ImGui::TreePop();
        }
        return anyChanged;
    }


    void RegisterCustomDrawer( const std::string& InCppName, CustomDrawer InDrawer )
    {
        Drawers()[InCppName] = std::move( InDrawer );
    }


    void RegisterFieldDecorator( const std::string& InTypeName, const std::string& InFieldName, std::vector<std::string> InFieldsItEdits, FieldDecorator InDecorator )
    {
        RegisterFieldDecorator( InTypeName, InFieldName, std::move( InFieldsItEdits ), std::move( InDecorator ), nullptr );
    }

    void RegisterFieldDecorator( const std::string& InTypeName, const std::string& InFieldName, std::vector<std::string> InFieldsItEdits, FieldDecorator InDecorator, DecoratorFixUp InFixUp )
    {
        Decorators()[InTypeName + "." + InFieldName].push_back( { std::move( InFieldsItEdits ), std::move( InDecorator ), std::move( InFixUp ) } );
    }


    MeWidgets::NumberSpec NumberSpecFor( const Reflection::FieldInfo& InField )
    {
        const json* previous = CurrentDefault();
        CurrentDefault() = nullptr;
        MeWidgets::NumberSpec spec = SpecFor( &InField, *InField.Ops );
        CurrentDefault() = previous;
        return spec;
    }


    bool EntityHandleValue( const char* InLabel, EntityHandle& InOutHandle );


    void RegisterDefaultDrawers()
    {
        // EntityHandle: shows the target's name, accepts hierarchy drags, click to select, x to clear.
        // Path: asset path with drag-drop from the asset browser and a picker.
        RegisterCustomDrawer( "Path", []( const char* InLabel, const Reflection::FieldInfo* InField, void* InValue ) {
            Path& path = *static_cast<Path*>( InValue );
            std::string text = path.GetLocalPathString();
            if( DrawAssetPath( InLabel, InField, text ) )
            {
                path = text.empty() ? Path() : Path( text );
                return true;
            }
            return false;
        } );

        // Shader graph property values (Decal, ParticleSystem, PostProcess): the graph's exposed
        // properties, laid out below the field's label.
        RegisterCustomDrawer( "ShaderGraphParams", []( const char*, const Reflection::FieldInfo*, void* InValue ) {
            return static_cast<Moonlight::ShaderGraphParams*>( InValue )->OnEditorInspect();
        } );
        RegisterCustomDrawer( "ShaderGraphParamsList", []( const char*, const Reflection::FieldInfo*, void* InValue ) {
            return static_cast<Moonlight::ShaderGraphParamsList*>( InValue )->OnEditorInspect();
        } );

        RegisterCustomDrawer( "EntityHandle", []( const char* InLabel, const Reflection::FieldInfo*, void* InValue ) {
            return EntityHandleValue( InLabel, *static_cast<EntityHandle*>( InValue ) );
        } );
    }


    bool EntityHandleValue( const char* InLabel, EntityHandle& InOutHandle )
    {
        EntityHandle& handle = InOutHandle;
        bool changed = false;
        ImGui::PushID( InLabel );
        const std::string name = handle ? ( handle->GetName().empty() ? std::string( "(unnamed)" ) : handle->GetName() ) : std::string( "None" );
        if( ImGui::Button( name.c_str(), ImVec2( std::max( ImGui::CalcItemWidth() - ImGuiTheme::IconButtonsWidth( 1 ), ImGuiTheme::Em( 2.f ) ), 0.f ) ) && handle )
        {
            Selection::Get().Set( handle );
        }
        ImGui::SetItemTooltip( "Drag an entity from the hierarchy here" );
        if( ImGui::BeginDragDropTarget() )
        {
            if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( kEntityPayload ) )
            {
                const ParentDescriptor* descriptor = static_cast<const ParentDescriptor*>( payload->Data );
                if( descriptor && descriptor->Parent )
                {
                    handle = descriptor->Parent->Parent;
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine( 0.f, ImGuiTheme::InnerGap() );
        if( ImGui::Button( "x", ImGuiTheme::IconButtonSize() ) && handle )
        {
            handle = EntityHandle();
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }


    // The script inspector's reference rows: the rows above, on the GUID/path representations
    // script fields store (installed into ScriptInspectorRows by the editor app).
    bool EntityGuidRow( const char* InLabel, uint64_t& InOutGuid )
    {
        World& world = EditorOps::GetWorld();
        EntityHandle handle = InOutGuid != 0 ? world.FindEntityByGUID( InOutGuid ) : EntityHandle();
        ImGui::PushID( InLabel );
        HavanaUtils::Label( InLabel );
        const bool changed = EntityHandleValue( "##entity", handle );
        ImGui::PopID();
        if( changed )
        {
            InOutGuid = handle ? handle->GetGUID() : 0;
        }
        return changed;
    }


    bool AssetPathRow( const char* InLabel, std::string& InOutPath, const char* InExtensions )
    {
        Reflection::FieldInfo field;
        field.AssetFilter = InExtensions ? InExtensions : "";
        ImGui::PushID( InLabel );
        HavanaUtils::Label( InLabel );
        const bool changed = DrawAssetPath( "##asset", &field, InOutPath );
        ImGui::PopID();
        return changed;
    }
}

#endif
