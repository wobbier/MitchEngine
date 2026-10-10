#include "ReflectionUI.h"

#if USING( ME_EDITOR )

#include "ECS/EntityHandle.h"
#include "ECS/Entity.h"
#include "Components/Transform.h"
#include "Events/EditorEvents.h"
#include "Math/Quaternion.h"
#include "Types/AssetDescriptor.h"
#include "Types/AssetType.h"
#include "Utils/CommonUtils.h"
#include "Selection.h"
#include <Utils/HavanaUtils.h>
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

        void Tooltip( const FieldInfo* InField )
        {
            if( InField && !InField->Tooltip.empty() && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
            {
                ImGui::SetTooltip( "%s", InField->Tooltip.c_str() );
            }
        }

        bool DrawValue( const char* InLabel, const FieldInfo* InField, const ValueOps& InOps, void* InValue, bool InMixed );

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

            if( ImGui::BeginCombo( InLabel, preview.c_str() ) )
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
            const float buttonWidth = ImGui::GetFrameHeight();
            const float fullWidth = ImGui::CalcItemWidth();
            ImGui::SetNextItemWidth( std::max( fullWidth - buttonWidth - 2.f, 40.f ) );
            changed |= ImGui::InputTextWithHint( "##path", "None", &InOutPath, ImGuiInputTextFlags_EnterReturnsTrue );
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
            ImGui::SameLine( 0.f, 2.f );
            // The picker answers asynchronously (a later frame); results are keyed by widget ID.
            const ImGuiID pickerId = ImGui::GetID( "picker" );
            if( ImGui::Button( "...", ImVec2( buttonWidth, 0.f ) ) )
            {
                const AssetType filter = InField ? AssetTypeFromFilter( InField->AssetFilter ) : AssetType::Unknown;
                RequestAssetSelectionEvent evt( [pickerId]( Path selected ) {
                    PickerResults()[pickerId] = selected.GetLocalPathString();
                }, filter );
                evt.Fire();
            }
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

        // N drag fields side by side with a coloured axis marker (X red, Y green, Z blue, W gray).
        bool DragAxes( const char* InLabel, float* InValues, int InCount, float InSpeed, const char* InFormat, bool* OutActive = nullptr )
        {
            static const ImU32 kAxisColors[4] = { IM_COL32( 230, 70, 80, 255 ), IM_COL32( 120, 200, 40, 255 ), IM_COL32( 50, 140, 250, 255 ), IM_COL32( 150, 150, 150, 255 ) };
            bool changed = false;
            ImGui::PushID( InLabel );
            const float spacing = 2.f;
            const float width = ( ImGui::CalcItemWidth() - spacing * ( InCount - 1 ) ) / InCount;
            for( int i = 0; i < InCount; ++i )
            {
                ImGui::PushID( i );
                if( i > 0 )
                {
                    ImGui::SameLine( 0.f, spacing );
                }
                ImGui::SetNextItemWidth( width );
                changed |= ImGui::DragFloat( "##axis", &InValues[i], InSpeed, 0.f, 0.f, InFormat );
                if( OutActive && ImGui::IsItemActive() )
                {
                    *OutActive = true;
                }
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled( min, ImVec2( min.x + 3.f, max.y ), kAxisColors[i], 2.f, ImDrawFlags_RoundCornersLeft );
                ImGui::PopID();
            }
            ImGui::PopID();
            return changed;
        }

        bool DrawVector( const char* InLabel, const FieldInfo* InField, const ValueOps& InOps, void* InValue )
        {
            const bool isColor = InField && InField->Has( FieldFlags::Color );
            const ImGuiColorEditFlags colorFlags = ImGuiColorEditFlags_Float | ( InField && InField->Has( FieldFlags::HDR ) ? ImGuiColorEditFlags_HDR : 0 );
            const float speed = InField ? InField->Speed : 0.1f;
            switch( InOps.Type )
            {
            case PropertyType::Vector2:
                return DragAxes( InLabel, &static_cast<Vector2*>( InValue )->x, 2, speed, "%.3f" );
            case PropertyType::Vector3:
            {
                Vector3& value = *static_cast<Vector3*>( InValue );
                if( isColor )
                {
                    return ImGui::ColorEdit3( InLabel, &value.x, colorFlags );
                }
                return DragAxes( InLabel, &value.x, 3, speed, "%.3f" );
            }
            case PropertyType::Vector4:
            {
                Vector4& value = *static_cast<Vector4*>( InValue );
                if( isColor )
                {
                    return ImGui::ColorEdit4( InLabel, &value.x, colorFlags | ImGuiColorEditFlags_AlphaBar );
                }
                return DragAxes( InLabel, &value.x, 4, speed, "%.3f" );
            }
            case PropertyType::Quaternion:
            {
                Quaternion& value = *static_cast<Quaternion*>( InValue );
                const ImGuiID id = ImGui::GetID( InLabel );
                auto& cache = EulerCache();
                Vector3 euler = cache.count( id ) ? cache[id] : Quaternion::ToEulerAngles( value );
                bool active = false;
                const bool changed = DragAxes( InLabel, &euler.x, 3, 0.5f, "%.2f", &active );
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
            const bool open = ImGui::TreeNodeEx( InLabel, ImGuiTreeNodeFlags_SpanAvailWidth, "%s [%zu]", InLabel, size );
            ImGui::SameLine( ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight() * 2.f - 4.f );
            ImGui::PushID( InLabel );
            if( ImGui::SmallButton( "+" ) )
            {
                InOps.ArrayResize( InValue, size + 1 );
                changed = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled( size == 0 );
            if( ImGui::SmallButton( "-" ) )
            {
                InOps.ArrayResize( InValue, size - 1 );
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
            if( open )
            {
                const size_t count = InOps.ArraySize( InValue );
                for( size_t i = 0; i < count; ++i )
                {
                    ImGui::PushID( static_cast<int>( i ) );
                    const std::string elementLabel = "[" + std::to_string( i ) + "]";
                    changed |= DrawValue( elementLabel.c_str(), nullptr, *InOps.Element, InOps.ArrayAt( InValue, i ), false );
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            return changed;
        }

        bool DrawFieldsOf( const TypeInfo& InType, void* InInstance )
        {
            Context nested;
            return DrawType( InType, InInstance, nested );
        }

        bool DrawValue( const char* InLabel, const FieldInfo* InField, const ValueOps& InOps, void* InValue, bool InMixed )
        {
            ImGui::PushID( InLabel );
            bool changed = false;

            // Structs and arrays get a tree node instead of a label column.
            if( InOps.Type == PropertyType::Struct )
            {
                if( ImGui::TreeNodeEx( InLabel, ImGuiTreeNodeFlags_SpanAvailWidth ) )
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
                ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.f, 0.8f, 0.3f, 1.f ) );
            }
            HavanaUtils::Label( InLabel );
            if( InMixed )
            {
                ImGui::PopStyleColor();
                if( ImGui::IsItemHovered() )
                {
                    ImGui::SetTooltip( "Selected entities have different values" );
                }
            }
            Tooltip( InField );
            ImGui::PushItemFlag( ImGuiItemFlags_MixedValue, InMixed );

            switch( InOps.Type )
            {
            case PropertyType::Bool:
                changed = ImGui::Checkbox( "##value", static_cast<bool*>( InValue ) );
                break;
            case PropertyType::Int:
            case PropertyType::UInt:
                changed = DrawInt( "##value", InField, InOps, InValue );
                break;
            case PropertyType::Float:
                changed = DrawFloat( "##value", InField, *static_cast<float*>( InValue ) );
                break;
            case PropertyType::Double:
                changed = ImGui::DragScalar( "##value", ImGuiDataType_Double, InValue, InField ? InField->Speed : 0.1f );
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
                    changed = ImGui::InputTextMultiline( "##value", &text, ImVec2( -1.f, ImGui::GetTextLineHeight() * 4.f ) );
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

            ImGui::PopItemFlag();
            ImGui::PopID();
            return changed;
        }
    }


    bool DrawType( const Reflection::TypeInfo& InType, void* InInstance, Context& InContext )
    {
        bool anyChanged = false;
        std::string currentCategory;
        bool categoryOpen = true;

        InType.ForEachField( InInstance, [&]( const Reflection::FieldInfo& field, void* owner ) {
            if( field.Has( Reflection::FieldFlags::Hidden ) )
            {
                return;
            }
            if( field.Category != currentCategory )
            {
                if( !currentCategory.empty() && categoryOpen )
                {
                    ImGui::TreePop();
                }
                currentCategory = field.Category;
                categoryOpen = currentCategory.empty() || ImGui::TreeNodeEx( currentCategory.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth );
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
            const float rowWidth = ImGui::GetContentRegionAvail().x;

            const bool readOnly = field.Has( Reflection::FieldFlags::ReadOnly );
            ImGui::BeginDisabled( readOnly );
            if( overridden )
            {
                ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.45f, 0.75f, 1.f, 1.f ) );
            }
            const bool changed = DrawValue( field.DisplayName.c_str(), &field, *field.Ops, field.Get( owner ), mixed );
            if( overridden )
            {
                ImGui::PopStyleColor();
            }
            ImGui::EndDisabled();

            // Prefab override marker and label context menu.
            const ImVec2 labelMax( rowStart.x + rowWidth * 0.35f, rowStart.y + ImGui::GetFrameHeight() );
            if( overridden )
            {
                ImGui::GetWindowDrawList()->AddRectFilled( ImVec2( rowStart.x - 6.f, rowStart.y + 1.f ), ImVec2( rowStart.x - 3.f, labelMax.y - 1.f ), IM_COL32( 60, 160, 255, 255 ) );
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

            if( changed )
            {
                anyChanged = true;
                InContext.ChangedFields.push_back( field.Name );
                if( !InContext.Others.empty() )
                {
                    const json value = Reflection::GetPathJson( InType, InInstance, field.Name );
                    for( void* other : InContext.Others )
                    {
                        Reflection::SetPathJson( InType, other, field.Name, value );
                    }
                }
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

        RegisterCustomDrawer( "EntityHandle", []( const char* InLabel, const Reflection::FieldInfo*, void* InValue ) {
            EntityHandle& handle = *static_cast<EntityHandle*>( InValue );
            bool changed = false;
            ImGui::PushID( InLabel );
            const float clearWidth = ImGui::GetFrameHeight();
            const std::string name = handle ? ( handle->GetName().empty() ? std::string( "(unnamed)" ) : handle->GetName() ) : std::string( "None" );
            if( ImGui::Button( name.c_str(), ImVec2( std::max( ImGui::CalcItemWidth() - clearWidth - 2.f, 40.f ), 0.f ) ) && handle )
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
            ImGui::SameLine( 0.f, 2.f );
            if( ImGui::Button( "x", ImVec2( clearWidth, 0.f ) ) && handle )
            {
                handle = EntityHandle();
                changed = true;
            }
            ImGui::PopID();
            return changed;
        } );
    }
}

#endif
