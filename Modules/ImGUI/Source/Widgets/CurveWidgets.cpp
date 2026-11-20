#include "CurveWidgets.h"
#include "ChoiceWidgets.h"
#include "Graph.h"
#include "ItemLog.h"
#include "Juice.h"
#include "Utils/HavanaUtils.h"
#include "ImGuiTheme.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>

namespace MeWidgets
{
    namespace CurveMath
    {
        namespace
        {
            double Secant( const Key& InA, const Key& InB )
            {
                const double dx = InB.X - InA.X;
                return dx > 0.0 ? ( InB.Y - InA.Y ) / dx : 0.0;
            }
        }

        const char* TangentName( Tangent InMode )
        {
            switch( InMode )
            {
            case Tangent::Linear: return "Linear";
            case Tangent::Flat: return "Flat";
            case Tangent::Free: return "Free";
            default: return "Auto";
            }
        }

        void UpdateTangents( std::vector<Key>& InOutKeys )
        {
            const int count = static_cast<int>( InOutKeys.size() );
            for( int i = 0; i < count; ++i )
            {
                Key& key = InOutKeys[i];
                const bool hasPrev = i > 0;
                const bool hasNext = i + 1 < count;
                const double prev = hasPrev ? Secant( InOutKeys[i - 1], key ) : 0.0;
                const double next = hasNext ? Secant( key, InOutKeys[i + 1] ) : 0.0;
                switch( key.Mode )
                {
                case Tangent::Free:
                    break;
                case Tangent::Flat:
                    key.In = key.Out = 0.0;
                    break;
                case Tangent::Linear:
                    key.In = hasPrev ? prev : next;
                    key.Out = hasNext ? next : prev;
                    break;
                case Tangent::Auto:
                default:
                {
                    double slope = 0.0;
                    if( hasPrev && hasNext )
                    {
                        // A peak or a valley stays level; elsewhere the neighbours' secant,
                        // limited so the curve never overshoots them (Fritsch-Carlson).
                        if( prev * next > 0.0 )
                        {
                            const double dx = InOutKeys[i + 1].X - InOutKeys[i - 1].X;
                            slope = dx > 0.0 ? ( InOutKeys[i + 1].Y - InOutKeys[i - 1].Y ) / dx : 0.0;
                            const double limit = 3.0 * std::min( std::abs( prev ), std::abs( next ) );
                            slope = std::clamp( slope, -limit, limit );
                        }
                    }
                    else if( hasNext )
                    {
                        slope = next;
                    }
                    else if( hasPrev )
                    {
                        slope = prev;
                    }
                    key.In = key.Out = slope;
                    break;
                }
                }
            }
        }

        double Evaluate( const std::vector<Key>& InKeys, double InX )
        {
            if( InKeys.empty() )
            {
                return 0.0;
            }
            if( InX <= InKeys.front().X )
            {
                return InKeys.front().Y;
            }
            if( InX >= InKeys.back().X )
            {
                return InKeys.back().Y;
            }
            size_t segment = 0;
            while( segment + 2 < InKeys.size() && InX >= InKeys[segment + 1].X )
            {
                ++segment;
            }
            const Key& a = InKeys[segment];
            const Key& b = InKeys[segment + 1];
            const double h = b.X - a.X;
            if( !( h > 0.0 ) )
            {
                return b.Y;
            }
            const double t = ( InX - a.X ) / h;
            const double t2 = t * t;
            const double t3 = t2 * t;
            const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
            const double h10 = t3 - 2.0 * t2 + t;
            const double h01 = -2.0 * t3 + 3.0 * t2;
            const double h11 = t3 - t2;
            return h00 * a.Y + h10 * h * a.Out + h01 * b.Y + h11 * h * b.In;
        }

        const char* PresetName( Preset InPreset )
        {
            switch( InPreset )
            {
            case Preset::Linear: return "Linear";
            case Preset::EaseIn: return "Ease In";
            case Preset::EaseOut: return "Ease Out";
            case Preset::EaseInOut: return "Ease In-Out";
            case Preset::Constant: return "Constant";
            default: return "";
            }
        }

        std::vector<Key> MakePreset( Preset InPreset, double InX0, double InX1, double InY0, double InY1 )
        {
            Key a;
            Key b;
            a.X = InX0;
            b.X = InX1;
            a.Y = InY0;
            b.Y = InY1;
            const double slope = InX1 > InX0 ? ( InY1 - InY0 ) / ( InX1 - InX0 ) : 0.0;
            switch( InPreset )
            {
            case Preset::EaseIn:
                a.Mode = Tangent::Flat;
                b.Mode = Tangent::Free;
                b.In = b.Out = 2.0 * slope;
                break;
            case Preset::EaseOut:
                a.Mode = Tangent::Free;
                a.In = a.Out = 2.0 * slope;
                b.Mode = Tangent::Flat;
                break;
            case Preset::EaseInOut:
                a.Mode = b.Mode = Tangent::Flat;
                break;
            case Preset::Constant:
                b.Y = InY0;
                a.Mode = b.Mode = Tangent::Flat;
                break;
            case Preset::Linear:
            default:
                a.Mode = b.Mode = Tangent::Linear;
                break;
            }
            std::vector<Key> keys = { a, b };
            UpdateTangents( keys );
            return keys;
        }

        int AddKey( std::vector<Key>& InOutKeys, double InX, double InY )
        {
            Key key;
            key.X = InX;
            key.Y = InY;
            size_t at = 0;
            while( at < InOutKeys.size() && InOutKeys[at].X <= InX )
            {
                ++at;
            }
            InOutKeys.insert( InOutKeys.begin() + static_cast<std::ptrdiff_t>( at ), key );
            UpdateTangents( InOutKeys );
            return static_cast<int>( at );
        }

        bool RemoveKey( std::vector<Key>& InOutKeys, int InIndex, int InMinKeys )
        {
            if( InIndex < 0 || InIndex >= static_cast<int>( InOutKeys.size() ) || static_cast<int>( InOutKeys.size() ) <= std::max( InMinKeys, 0 ) )
            {
                return false;
            }
            InOutKeys.erase( InOutKeys.begin() + InIndex );
            UpdateTangents( InOutKeys );
            return true;
        }

        std::string DescribeKey( const Key& InKey, Unit InXUnit, Unit InYUnit )
        {
            return FormatValue( InXUnit, InKey.X ) + ", " + FormatValue( InYUnit, InKey.Y );
        }

        std::string Describe( const std::vector<Key>& InKeys, Unit InXUnit, Unit InYUnit )
        {
            std::string text;
            for( const Key& key : InKeys )
            {
                if( !text.empty() )
                {
                    text += " ";
                }
                text += "(" + DescribeKey( key, InXUnit, InYUnit ) + ")";
            }
            return text;
        }
    }

    namespace
    {
        using CurveMath::Key;
        using CurveMath::Tangent;

        struct CurveState
        {
            int Selected = -1;
            bool Focused = false;
            // One of the editor's handles was held last frame: the Y axis holds still.
            bool Active = false;
            double HeldTop = 0.0;
            int MenuKey = -1;
            // The end values the curve had before it went level (Constant): Linear and the
            // eases go back to them rather than to the axis.
            bool HasEnds = false;
            double EndStart = 0.0;
            double EndFinish = 1.0;
        };

        std::unordered_map<ImGuiID, CurveState>& States()
        {
            static std::unordered_map<ImGuiID, CurveState> states;
            return states;
        }

        float Thin()
        {
            return std::max( 1.f, std::round( ImGui::GetFontSize() / 14.f ) );
        }

        ImU32 U32( const ImVec4& InColor )
        {
            return ImGui::GetColorU32( InColor );
        }

        void Range( const NumberSpec& InSpec, double InDefaultMin, double InDefaultMax, double& OutMin, double& OutMax )
        {
            OutMin = InSpec.HasRange ? InSpec.Min : InDefaultMin;
            OutMax = InSpec.HasRange ? InSpec.Max : InDefaultMax;
            if( !( OutMax > OutMin ) )
            {
                OutMax = OutMin + 1.0;
            }
        }

        bool SameKeys( const std::vector<Key>& InA, const std::vector<Key>& InB )
        {
            if( InA.size() != InB.size() )
            {
                return false;
            }
            for( size_t i = 0; i < InA.size(); ++i )
            {
                const double tolerance = 1e-6;
                if( std::abs( InA[i].X - InB[i].X ) > tolerance || std::abs( InA[i].Y - InB[i].Y ) > tolerance
                    || std::abs( InA[i].In - InB[i].In ) > tolerance || std::abs( InA[i].Out - InB[i].Out ) > tolerance )
                {
                    return false;
                }
            }
            return true;
        }

        // A preset's shape, small, in a button.
        void PresetGlyph( ImDrawList* InDrawList, const ImRect& InRect, CurveMath::Preset InPreset, ImU32 InColor )
        {
            const std::vector<Key> keys = CurveMath::MakePreset( InPreset, 0.0, 1.0, 0.0, 1.0 );
            const float padX = std::round( InRect.GetWidth() * 0.22f );
            const float padY = std::round( InRect.GetHeight() * 0.24f );
            const ImVec2 min( InRect.Min.x + padX, InRect.Min.y + padY );
            const ImVec2 max( InRect.Max.x - padX, InRect.Max.y - padY );
            constexpr int kSamples = 16;
            ImVec2 points[kSamples + 1];
            for( int i = 0; i <= kSamples; ++i )
            {
                const double t = static_cast<double>( i ) / kSamples;
                const double y = CurveMath::Evaluate( keys, t );
                // Constant holds its start value: drawn halfway up so it reads as a level line.
                const double shown = InPreset == CurveMath::Preset::Constant ? 0.5 : y;
                points[i] = ImVec2( min.x + static_cast<float>( t ) * ( max.x - min.x ), max.y - static_cast<float>( shown ) * ( max.y - min.y ) );
            }
            InDrawList->AddPolyline( points, kSamples + 1, InColor, ImDrawFlags_None, Thin() * 1.5f );
        }
    }

    bool CurveEditor( const char* InCaption, std::vector<Key>& InOutKeys, const CurveOptions& InOptions )
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const int minKeys = std::max( InOptions.MinKeys, 1 );
        const int maxKeys = std::max( InOptions.MaxKeys, minKeys );
        bool changed = false;

        ImGui::PushID( InCaption );
        const ImGuiID stateId = window->GetID( "##curve" );
        CurveState& state = States()[stateId];
        const int count = static_cast<int>( InOutKeys.size() );
        if( state.Selected < 0 || state.Selected >= count )
        {
            state.Selected = count - 1;
        }
        auto locked = [&]( int InIndex ) { return InIndex >= 0 && InIndex < 32 && ( InOptions.LockedKeys & ( 1u << InIndex ) ) != 0; };
        const bool canAdd = !InOptions.LockX && count < maxKeys;
        const bool canRemove = !InOptions.LockX && count > minKeys;

        double xMin = 0.0;
        double xMax = 1.0;
        Range( InOptions.X, 0.0, 1.0, xMin, xMax );
        double yMin = 0.0;
        double yMax = 1.0;
        Range( InOptions.Y, 0.0, 1.0, yMin, yMax );
        double yTop = yMax;
        if( InOptions.FitY )
        {
            if( state.Active )
            {
                yTop = state.HeldTop;
            }
            else
            {
                double highest = yMin;
                for( const Key& key : InOutKeys )
                {
                    highest = std::max( highest, key.Y );
                }
                const double span = std::max( ( highest - yMin ) * 1.25, 1e-6 );
                const double step = NiceStep( span ) * 5.0;
                yTop = std::min( yMin + std::ceil( span / step - 1e-9 ) * step, yMax );
                if( !( yTop > yMin ) )
                {
                    yTop = yMin + 1.0;
                }
                state.HeldTop = yTop;
            }
        }

        const float width = HavanaUtils::Label( InCaption );
        ItemLog::PushScope( InCaption );
        const Graph::Axis xAxis { xMin, xMax, false, InOptions.X.FieldUnit };
        const Graph::Axis yAxis { yMin, yTop, false, InOptions.Y.FieldUnit };
        const float height = ImGuiTheme::Em( InOptions.Height > 0.f ? InOptions.Height : 6.f );
        // A refused add or delete shakes the whole editor sideways (Juice).
        const float shake = Juice::ShakeOffset( Graph::HandleId( InCaption, "##canvas" ) );
        if( shake != 0.f )
        {
            ImGui::SetCursorScreenPos( ImVec2( ImGui::GetCursorScreenPos().x + shake, ImGui::GetCursorScreenPos().y ) );
        }
        const bool visible = Graph::Begin( InCaption, ImVec2( width, height ), xAxis, yAxis );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if( visible )
        {
            Graph::Grid( true, true );
            Graph::Plot( [&]( double InX ) { return CurveMath::Evaluate( InOutKeys, InX ); }, U32( palette.AccentText ), true, 128 );
        }

        // The selected key's tangent handles: where they sit (a fixed length on screen along
        // each slope), with their lines drawn under the keys.
        const int selected = state.Selected;
        const bool tangents = InOptions.Tangents && selected >= 0 && selected < count && !locked( selected );
        const float sx = ( map.Max.x - map.Min.x ) / static_cast<float>( xMax - xMin );
        const float sy = ( map.Max.y - map.Min.y ) / static_cast<float>( yTop - yMin );
        auto handlePoint = [&]( const Key& InKey, double InSlope, float InSide ) {
            // Screen direction of the slope, then back to axis units.
            float dx = 1.f;
            float dy = static_cast<float>( InSlope ) * sy / std::max( sx, 1e-6f );
            const float length = std::sqrt( dx * dx + dy * dy );
            dx /= length;
            dy /= length;
            const float reach = ImGuiTheme::Em( 2.2f );
            const ImVec2 at = map.ToScreen( InKey.X, InKey.Y );
            const ImVec2 screen( at.x + InSide * dx * reach, at.y - InSide * dy * reach );
            return screen;
        };
        ImVec2 inPoint;
        ImVec2 outPoint;
        const bool hasIn = tangents && selected > 0;
        const bool hasOut = tangents && selected + 1 < count;
        if( tangents )
        {
            const Key& key = InOutKeys[selected];
            const ImVec2 at = map.ToScreen( key.X, key.Y );
            inPoint = handlePoint( key, key.In, -1.f );
            outPoint = handlePoint( key, key.Out, 1.f );
            const ImU32 line = U32( ImGuiTheme::WithAlpha( palette.Text, 0.55f ) );
            if( visible && hasIn )
            {
                drawList->AddLine( at, inPoint, line, Thin() );
            }
            if( visible && hasOut )
            {
                drawList->AddLine( at, outPoint, line, Thin() );
            }
        }

        bool anyActive = false;
        bool anyHovered = false;
        bool keysMoved = false;
        int removeIndex = -1;
        const double gap = ( xMax - xMin ) * 0.005;
        const double step = NiceStep( std::min( xMax - xMin, yTop - yMin ) ) * 0.1;
        for( int i = 0; i < count; ++i )
        {
            Key& key = InOutKeys[i];
            const bool fixed = locked( i );
            Graph::HandleSpec spec;
            spec.MinX = i > 0 ? InOutKeys[i - 1].X + gap : xMin;
            spec.MaxX = i + 1 < count ? InOutKeys[i + 1].X - gap : xMax;
            if( InOptions.LockX || fixed )
            {
                spec.MinX = spec.MaxX = key.X;
            }
            spec.MinY = InOptions.Y.HasRange ? yMin : -1e300;
            spec.MaxY = InOptions.Y.HasRange ? yMax : 1e300;
            if( fixed )
            {
                spec.MinY = spec.MaxY = key.Y;
            }
            spec.ReportY = true;
            spec.FreezeAxis = true;
            spec.Step = step;
            spec.Color = fixed ? U32( palette.Muted ) : 0;
            const std::string label = "Key " + std::to_string( i + 1 );
            double x = key.X;
            double y = key.Y;
            if( Graph::DragHandle( label.c_str(), &x, &y, spec ) )
            {
                key.X = x;
                key.Y = y;
                keysMoved = true;
                changed = true;
            }
            const ImGuiID id = g.LastItemData.ID;
            ItemLog::SetItemValue( id, CurveMath::DescribeKey( key, InOptions.X.FieldUnit, InOptions.Y.FieldUnit ) );
            if( ImGui::IsItemActivated() )
            {
                state.Selected = i;
                state.Focused = true;
            }
            const bool hovered = ImGui::IsItemHovered();
            anyHovered |= hovered;
            anyActive |= ImGui::IsItemActive();
            if( hovered && ImGui::IsMouseReleased( ImGuiMouseButton_Right ) )
            {
                state.Selected = i;
                state.MenuKey = i;
                ImGui::OpenPopup( "##keymenu" );
            }
            if( hovered && !ImGui::IsItemActive() && fixed )
            {
                ImGui::SetTooltip( "%s is fixed at %s", label.c_str(), CurveMath::DescribeKey( key, InOptions.X.FieldUnit, InOptions.Y.FieldUnit ).c_str() );
            }
        }
        if( keysMoved )
        {
            CurveMath::UpdateTangents( InOutKeys );
        }

        if( tangents && state.Selected == selected )
        {
            Key& key = InOutKeys[selected];
            auto tangentHandle = [&]( const char* InName, const ImVec2& InPoint, float InSide ) {
                Graph::HandleSpec spec;
                spec.ShowValue = false;
                spec.FreezeAxis = true;
                spec.Color = U32( palette.Text );
                if( InSide > 0.f )
                {
                    spec.MinX = key.X;
                }
                else
                {
                    spec.MaxX = key.X;
                }
                double hx = map.XFromScreen( InPoint.x );
                double hy = map.YFromScreen( InPoint.y );
                const std::string label = std::string( InName ) + "##" + std::to_string( selected + 1 );
                ItemLog::PushScope( InCaption, "Key " + std::to_string( selected + 1 ) );
                if( Graph::DragHandle( label.c_str(), &hx, &hy, spec ) )
                {
                    const double dx = std::max( InSide * ( hx - key.X ), ( xMax - xMin ) * 1e-4 );
                    const double slope = InSide * ( hy - key.Y ) / dx;
                    if( InSide > 0.f )
                    {
                        key.Out = slope;
                    }
                    else
                    {
                        key.In = slope;
                    }
                    key.Mode = Tangent::Free;
                    CurveMath::UpdateTangents( InOutKeys );
                    changed = true;
                }
                ItemLog::MarkLastItemRowScoped();
                ItemLog::PopScope();
                ItemLog::SetLastItemValue( "slope " + FormatNumber( InSide > 0.f ? key.Out : key.In, 3 ) );
                anyHovered |= ImGui::IsItemHovered();
                anyActive |= ImGui::IsItemActive();
                if( ImGui::IsItemActivated() )
                {
                    state.Focused = true;
                }
                if( ImGui::IsItemHovered() && !ImGui::IsItemActive() )
                {
                    ImGui::SetTooltip( "Drag to bend the curve %s %s (its tangents become Free)", InSide > 0.f ? "leaving" : "arriving at", ( "Key " + std::to_string( selected + 1 ) ).c_str() );
                }
            };
            if( hasIn )
            {
                tangentHandle( "In", inPoint, -1.f );
            }
            if( hasOut )
            {
                tangentHandle( "Out", outPoint, 1.f );
            }
        }

        // An added key flashes the plot's rim.
        const float flash = visible ? Juice::FlashAmount( canvasId ) : 0.f;
        if( flash > 0.f )
        {
            drawList->AddRect( ImVec2( map.Min.x - Thin(), map.Min.y - Thin() ), ImVec2( map.Max.x + Thin(), map.Max.y + Thin() ), U32( ImGuiTheme::WithAlpha( palette.Text, flash ) ), 0.f, 0, Thin() * 2.f );
        }

        const Graph::CanvasInput input = Graph::End();
        anyHovered |= input.Hovered;
        anyActive |= input.Held;
        state.Active = anyActive;
        if( input.Pressed )
        {
            state.Focused = true;
        }
        if( input.Hovered && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
        {
            if( canAdd )
            {
                const double x = std::clamp( input.X, xMin, xMax );
                const double y = std::clamp( input.Y, InOptions.Y.HasRange ? yMin : -1e300, InOptions.Y.HasRange ? yMax : 1e300 );
                const double rounded = step > 0.0 ? std::round( y / step ) * step : y;
                state.Selected = CurveMath::AddKey( InOutKeys, step > 0.0 ? std::round( x / step ) * step : x, rounded );
                changed = true;
                Juice::Flash( canvasId );
                Juice::Play( Juice::Cue::Commit );
                ImGui::MarkItemEdited( canvasId );
            }
            else
            {
                Juice::Shake( canvasId ); // Plays the Refuse cue too.
            }
        }
        ItemLog::SetItemValue( canvasId, CurveMath::Describe( InOutKeys, InOptions.X.FieldUnit, InOptions.Y.FieldUnit ) );
        if( input.Hovered && !input.Held )
        {
            const char* hint = InOptions.LockX ? "Drag a key up or down."
                : "Drag a key; double-click to add one; Delete or right-click removes the selected key.";
            ImGui::SetTooltip( "%s%s%s", InOptions.Tooltip ? InOptions.Tooltip : "", InOptions.Tooltip ? "\n" : "", hint );
        }

        // A key's menu: its tangents and Delete.
        if( ImGui::BeginPopup( "##keymenu" ) )
        {
            const int menuKey = state.MenuKey;
            if( menuKey >= 0 && menuKey < static_cast<int>( InOutKeys.size() ) )
            {
                ImGui::TextDisabled( "Key %d", menuKey + 1 );
                ImGui::Separator();
                if( InOptions.Tangents && !locked( menuKey ) )
                {
                    for( int mode = 0; mode < 4; ++mode )
                    {
                        const Tangent tangent = static_cast<Tangent>( mode );
                        if( ImGui::MenuItem( CurveMath::TangentName( tangent ), nullptr, InOutKeys[menuKey].Mode == tangent ) )
                        {
                            InOutKeys[menuKey].Mode = tangent;
                            CurveMath::UpdateTangents( InOutKeys );
                            changed = true;
                        }
                    }
                    ImGui::Separator();
                }
                if( ImGui::MenuItem( "Delete Key", "Del", false, canRemove && !locked( menuKey ) ) )
                {
                    removeIndex = menuKey;
                }
            }
            ImGui::EndPopup();
        }

        if( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && !anyHovered )
        {
            state.Focused = false;
        }
        if( state.Focused && !g.IO.WantTextInput && ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows )
            && ( ImGui::IsKeyPressed( ImGuiKey_Delete, false ) || ImGui::IsKeyPressed( ImGuiKey_Backspace, false ) ) )
        {
            if( canRemove && !locked( state.Selected ) )
            {
                removeIndex = state.Selected;
            }
            else
            {
                Juice::Shake( canvasId ); // Plays the Refuse cue too.
            }
        }
        if( removeIndex >= 0 && CurveMath::RemoveKey( InOutKeys, removeIndex, minKeys ) )
        {
            state.Selected = std::min( removeIndex, static_cast<int>( InOutKeys.size() ) - 1 );
            changed = true;
            Juice::Play( Juice::Cue::Reset );
            ImGui::MarkItemEdited( canvasId );
        }
        ItemLog::PopScope();

        // Presets: reshape between the curve's end values.
        if( InOptions.Presets && !InOptions.LockX )
        {
            ItemLog::PushScope( InCaption, "Preset" );
            const float rowWidth = HavanaUtils::SkipLabel();
            const float gapX = ImGuiTheme::InnerGap();
            const float frameHeight = ImGui::GetFrameHeight();
            const int presets = static_cast<int>( CurveMath::Preset::Count );
            const float resetWidth = ImGuiTheme::ButtonWidth( "Reset" );
            const float buttonWidth = std::floor( std::min( frameHeight * 1.6f, ( rowWidth - resetWidth - gapX * presets ) / presets ) );
            const double x0 = InOutKeys.size() >= 2 ? InOutKeys.front().X : xMin;
            const double x1 = InOutKeys.size() >= 2 ? InOutKeys.back().X : xMax;
            const double start = InOutKeys.empty() ? yMin : InOutKeys.front().Y;
            double y0 = start;
            double y1 = InOutKeys.empty() ? yTop : InOutKeys.back().Y;
            if( std::abs( y1 - y0 ) >= 1e-9 )
            {
                state.HasEnds = true;
                state.EndStart = y0;
                state.EndFinish = y1;
            }
            else if( state.HasEnds && std::abs( state.EndStart - y0 ) < 1e-9 )
            {
                // Level at the start it had (Constant): ease back between its old ends.
                // A curve never seen with distinct ends falls through to the axis.
                y1 = state.EndFinish;
            }
            else
            {
                // A level curve with no end values to ease between: use the axis.
                y0 = yMin;
                y1 = yTop;
            }
            for( int p = 0; p < presets; ++p )
            {
                const CurveMath::Preset preset = static_cast<CurveMath::Preset>( p );
                const char* name = CurveMath::PresetName( preset );
                if( p > 0 )
                {
                    ImGui::SameLine( 0.f, gapX );
                }
                // Constant holds the start value whatever the shape, so pressing it again is a no-op.
                const std::vector<Key> shaped = preset == CurveMath::Preset::Constant ? CurveMath::MakePreset( preset, x0, x1, start, start ) : CurveMath::MakePreset( preset, x0, x1, y0, y1 );
                const bool current = SameKeys( shaped, InOutKeys );
                const bool pressed = ImGui::InvisibleButton( name, ImVec2( buttonWidth, frameHeight ) );
                ItemLog::MarkLastItemRowScoped();
                ItemLog::SetLastItemValue( current ? "current" : "" );
                const bool hovered = ImGui::IsItemHovered();
                const bool held = ImGui::IsItemActive();
                const ImRect rect( ImGui::GetItemRectMin(), ImGui::GetItemRectMax() );
                if( ImGui::IsItemVisible() )
                {
                    const ImGuiCol background = held ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg;
                    ImGui::RenderFrame( rect.Min, rect.Max, ImGui::GetColorU32( background ), true, g.Style.FrameRounding );
                    if( current )
                    {
                        drawList->AddRect( rect.Min, rect.Max, U32( palette.AccentText ), g.Style.FrameRounding, 0, Thin() * 1.5f );
                    }
                    const float flash = Juice::FlashAmount( ImGui::GetItemID() );
                    if( flash > 0.f )
                    {
                        drawList->AddRect( rect.Min, rect.Max, U32( ImGuiTheme::WithAlpha( palette.Text, flash ) ), g.Style.FrameRounding, 0, Thin() * 2.f );
                    }
                    PresetGlyph( drawList, rect, preset, U32( current ? palette.AccentText : hovered ? palette.Text : palette.Muted ) );
                }
                if( hovered )
                {
                    ImGui::SetTooltip( "%s%s", name, preset == CurveMath::Preset::Constant ? ": the start value all along" : "" );
                }
                if( pressed )
                {
                    InOutKeys = shaped;
                    state.Selected = static_cast<int>( InOutKeys.size() ) - 1;
                    changed = true;
                    Juice::Flash( ImGui::GetItemID() );
                    Juice::Play( Juice::Cue::Commit );
                    ImGui::MarkItemEdited( ImGui::GetItemID() );
                }
            }
            ImGui::SameLine( 0.f, gapX );
            if( ImGui::Button( "Reset", ImVec2( resetWidth, 0.f ) ) )
            {
                InOutKeys = InOptions.ResetKeys ? *InOptions.ResetKeys : CurveMath::MakePreset( CurveMath::Preset::Linear, xMin, xMax, yMin, yTop );
                state.Selected = static_cast<int>( InOutKeys.size() ) - 1;
                changed = true;
                Juice::Play( Juice::Cue::Reset );
            }
            ItemLog::MarkLastItemRowScoped();
            ImGui::SetItemTooltip( "Back to the curve's default" );
            ItemLog::PopScope();
        }

        // The selected key: its X, Y and tangents.
        if( state.Selected >= 0 && state.Selected < static_cast<int>( InOutKeys.size() ) && InOptions.KeyRow )
        {
            const int index = state.Selected;
            Key& key = InOutKeys[index];
            const std::string name = "Key " + std::to_string( index + 1 );
            ItemLog::PushScope( InCaption, name );
            const float rowWidth = HavanaUtils::Label( name );
            const float gapX = ImGuiTheme::InnerGap();
            const float fieldWidth = std::floor( ( rowWidth - gapX ) * 0.5f );
            const bool fixed = locked( index );
            ImGui::PushID( index );

            ImGui::BeginDisabled( fixed || InOptions.LockX );
            NumberSpec xSpec = InOptions.X;
            xSpec.HasRange = true;
            xSpec.Min = index > 0 ? InOutKeys[index - 1].X + gap : xMin;
            xSpec.Max = index + 1 < static_cast<int>( InOutKeys.size() ) ? InOutKeys[index + 1].X - gap : xMax;
            xSpec.Max = std::max( xSpec.Max, xSpec.Min );
            double x = key.X;
            const ImGuiID xId = window->GetID( "X##key" );
            ImGui::SetNextItemWidth( fieldWidth );
            if( UnitField( "X##key", x, xSpec ) )
            {
                key.X = x;
                CurveMath::UpdateTangents( InOutKeys );
                changed = true;
            }
            ItemLog::SetItemLabel( xId, "X" );
            ItemLog::MarkItemRowScoped( xId );
            ImGui::EndDisabled();

            ImGui::SameLine( 0.f, gapX );
            ImGui::BeginDisabled( fixed );
            NumberSpec ySpec = InOptions.Y;
            double y = key.Y;
            const ImGuiID yId = window->GetID( "Y##key" );
            ImGui::SetNextItemWidth( HavanaUtils::RowWidth() );
            if( UnitField( "Y##key", y, ySpec ) )
            {
                key.Y = y;
                CurveMath::UpdateTangents( InOutKeys );
                changed = true;
            }
            ItemLog::SetItemLabel( yId, "Y" );
            ItemLog::MarkItemRowScoped( yId );

            // The tangent modes on their own row: four whole names need the width.
            if( InOptions.Tangents )
            {
                HavanaUtils::Label( "Tangents" );
                static const char* const kModes[] = { "Auto", "Linear", "Flat", "Free" };
                int mode = static_cast<int>( key.Mode );
                if( Segmented( "##tangent", kModes, nullptr, 4, mode, false, "How the key's tangents are made" ) )
                {
                    key.Mode = static_cast<Tangent>( mode );
                    CurveMath::UpdateTangents( InOutKeys );
                    changed = true;
                }
            }
            ImGui::EndDisabled();
            ImGui::PopID();
            ItemLog::PopScope();
        }
        ImGui::PopID();
        return changed;
    }

    void ResetCurveEditors()
    {
        States().clear();
    }
}
