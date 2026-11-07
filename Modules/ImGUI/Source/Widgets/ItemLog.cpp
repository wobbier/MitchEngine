#include "ItemLog.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace MeWidgets::ItemLog
{
    namespace
    {
        struct WindowState
        {
            std::string Row;
            ImRect RowRect;
            bool HasRow = false;
            std::string Group;
            // Open tree nodes above the cursor, innermost last, with the tree depth inside each.
            // A collapsing header (no tree push) replaces the stack at depth -1.
            std::vector<std::pair<std::string, int>> Sections;
        };

        struct State
        {
            bool Enabled = false;
            std::vector<ImGuiContext*> Hooked;
            std::vector<Item> Building;
            std::vector<Item> Last;
            std::unordered_map<ImGuiID, WindowState> Windows;
            // Labels seen on earlier frames: clipped widgets skip their label hook, so they keep
            // the name they had when visible (and can still be found and scrolled to).
            std::unordered_map<ImGuiID, std::string> KnownLabels;
        };

        State& Get()
        {
            static State state;
            return state;
        }

        bool EqualsNoCase( std::string_view InA, std::string_view InB )
        {
            return InA.size() == InB.size() && std::equal( InA.begin(), InA.end(), InB.begin(), []( char a, char b ) {
                return std::tolower( static_cast<unsigned char>( a ) ) == std::tolower( static_cast<unsigned char>( b ) );
            } );
        }

        // "Position (m)" -> "Position": labels that still carry their unit answer to the bare name.
        std::string_view WithoutUnit( std::string_view InText )
        {
            if( InText.size() > 3 && InText.back() == ')' )
            {
                const size_t open = InText.rfind( " (" );
                if( open != std::string_view::npos && open > 0 )
                {
                    return InText.substr( 0, open );
                }
            }
            return InText;
        }

        bool NameMatches( std::string_view InText, std::string_view InName )
        {
            return !InText.empty() && ( EqualsNoCase( InText, InName ) || EqualsNoCase( WithoutUnit( InText ), InName ) );
        }

        std::string_view Trim( std::string_view InText )
        {
            while( !InText.empty() && std::isspace( static_cast<unsigned char>( InText.front() ) ) ) InText.remove_prefix( 1 );
            while( !InText.empty() && std::isspace( static_cast<unsigned char>( InText.back() ) ) ) InText.remove_suffix( 1 );
            return InText;
        }

        void OnNewFrame( ImGuiContext*, ImGuiContextHook* )
        {
            State& state = Get();
            state.Last.swap( state.Building );
            state.Building.clear();
            state.Windows.clear();
        }

        void InstallHook( ImGuiContext* InContext )
        {
            State& state = Get();
            if( !InContext || std::find( state.Hooked.begin(), state.Hooked.end(), InContext ) != state.Hooked.end() )
            {
                return;
            }
            ImGuiContextHook hook;
            hook.Type = ImGuiContextHookType_NewFramePre;
            hook.Callback = &OnNewFrame;
            ImGui::AddContextHook( InContext, &hook );
            // Forget the context when it's destroyed, so a new one at the same address is hooked.
            ImGuiContextHook shutdown;
            shutdown.Type = ImGuiContextHookType_Shutdown;
            shutdown.Callback = []( ImGuiContext* InDying, ImGuiContextHook* ) {
                auto& hooked = Get().Hooked;
                hooked.erase( std::remove( hooked.begin(), hooked.end(), InDying ), hooked.end() );
            };
            ImGui::AddContextHook( InContext, &shutdown );
            state.Hooked.push_back( InContext );
        }

        Item* FindBuilding( ImGuiID InId )
        {
            auto& items = Get().Building;
            // The item being described is almost always the last one added.
            for( size_t i = items.size(), steps = 0; i > 0 && steps < 8; --i, ++steps )
            {
                if( items[i - 1].Id == InId )
                {
                    return &items[i - 1];
                }
            }
            return nullptr;
        }
    }

    void Enable( bool InEnabled )
    {
        State& state = Get();
        state.Enabled = InEnabled;
        if( ImGuiContext* context = ImGui::GetCurrentContext() )
        {
            context->TestEngineHookItems = InEnabled;
            if( InEnabled )
            {
                InstallHook( context );
            }
        }
        if( !InEnabled )
        {
            state.Building.clear();
            state.Last.clear();
            state.Windows.clear();
            state.KnownLabels.clear();
        }
    }

    bool IsEnabled()
    {
        return Get().Enabled;
    }

    void Record( ImGuiContext* InContext, ImGuiID InId, const ImRect& InRect )
    {
        State& state = Get();
        if( !state.Enabled || !InContext || InId == 0 )
        {
            return;
        }
        ImGuiWindow* window = InContext->CurrentWindow;
        if( !window )
        {
            return;
        }
        if( !state.Building.empty() && state.Building.back().Id == InId )
        {
            return;
        }
        Item item;
        item.Id = InId;
        item.Rect = InRect;
        item.ClipRect = window->ClipRect;
        item.TreeDepth = window->DC.TreeDepth;
        item.IsWindow = InId == window->ID;
        ImGuiWindow* root = window->RootWindow ? window->RootWindow : window;
        item.Window = root->Name;
        WindowState& windowState = state.Windows[root->ID];
        // Tree nodes closed since (TreePop) no longer scope what follows.
        auto& sections = windowState.Sections;
        while( !sections.empty() && sections.back().second >= 0 && sections.back().second > window->DC.TreeDepth )
        {
            sections.pop_back();
        }
        item.Group = windowState.Group;
        item.Section = sections.empty() ? windowState.Group : sections.back().first;
        if( windowState.HasRow && InRect.Min.y < windowState.RowRect.Max.y && InRect.Max.y > windowState.RowRect.Min.y )
        {
            item.Row = windowState.Row;
        }
        if( InRect.Overlaps( window->ClipRect ) )
        {
            item.Flags |= ImGuiItemStatusFlags_Visible;
        }
        else
        {
            auto known = state.KnownLabels.find( InId );
            if( known != state.KnownLabels.end() )
            {
                item.Label = known->second;
            }
        }
        state.Building.push_back( std::move( item ) );
    }

    void Describe( ImGuiContext* InContext, ImGuiID InId, const char* InLabel, ImGuiItemStatusFlags InFlags )
    {
        State& state = Get();
        if( !state.Enabled )
        {
            return;
        }
        Item* item = FindBuilding( InId );
        if( !item )
        {
            return;
        }
        item->Flags |= InFlags & ~ImGuiItemStatusFlags_Visible;
        if( !item->IsRowLabel && InLabel )
        {
            item->Label = VisibleLabel( InLabel );
            if( !item->Label.empty() )
            {
                if( state.KnownLabels.size() > 65536 )
                {
                    state.KnownLabels.clear();
                }
                state.KnownLabels[InId] = item->Label;
            }
        }
        // An open tree node scopes the items inside it; a closed one (or a menu) scopes nothing.
        if( ( InFlags & ImGuiItemStatusFlags_Openable ) && ( InFlags & ImGuiItemStatusFlags_Opened ) && InContext && InContext->CurrentWindow
            && !item->Label.empty() && !item->IsWindow )
        {
            ImGuiWindow* window = InContext->CurrentWindow;
            if( window->Flags & ImGuiWindowFlags_MenuBar && window->DC.NavLayerCurrent == ImGuiNavLayer_Menu )
            {
                return;
            }
            ImGuiWindow* root = window->RootWindow ? window->RootWindow : window;
            auto& sections = state.Windows[root->ID].Sections;
            if( window->DC.TreeDepth > item->TreeDepth )
            {
                // TreeNode pushed: its scope ends when the tree pops back to this depth.
                sections.emplace_back( item->Label, window->DC.TreeDepth );
            }
            else
            {
                // CollapsingHeader: no push, so it scopes everything until the next header.
                sections.clear();
                sections.emplace_back( item->Label, -1 );
            }
        }
    }

    void MarkRowLabel( const char* InText )
    {
        State& state = Get();
        ImGuiContext* context = ImGui::GetCurrentContext();
        if( !state.Enabled || !context || !InText )
        {
            return;
        }
        const ImGuiID id = context->LastItemData.ID;
        Item* item = FindBuilding( id );
        if( !item )
        {
            return;
        }
        item->IsRowLabel = true;
        item->Label = VisibleLabel( InText );
        item->Row = item->Label;
        ImGuiWindow* window = context->CurrentWindow;
        ImGuiWindow* root = window->RootWindow ? window->RootWindow : window;
        WindowState& windowState = state.Windows[root->ID];
        windowState.Row = item->Label;
        windowState.RowRect = item->Rect;
        windowState.HasRow = true;
    }

    void SetItemValue( ImGuiID InId, std::string_view InValue )
    {
        if( !Get().Enabled )
        {
            return;
        }
        auto& items = Get().Building;
        for( size_t i = items.size(); i > 0; --i )
        {
            if( items[i - 1].Id == InId )
            {
                items[i - 1].Value = std::string( InValue );
                return;
            }
        }
    }

    void MarkSection( const char* InName )
    {
        ImGuiContext* context = ImGui::GetCurrentContext();
        if( !Get().Enabled || !context || !InName || !context->CurrentWindow )
        {
            return;
        }
        const std::string name = VisibleLabel( InName );
        ImGuiWindow* window = context->CurrentWindow;
        ImGuiWindow* root = window->RootWindow ? window->RootWindow : window;
        WindowState& windowState = Get().Windows[root->ID];
        windowState.Group = name;
        windowState.Sections.clear();
        if( Item* item = FindBuilding( context->LastItemData.ID ) )
        {
            item->Label = name;
            item->Group = name;
            item->Section = name;
        }
    }

    void EndSection()
    {
        ImGuiContext* context = ImGui::GetCurrentContext();
        if( !Get().Enabled || !context || !context->CurrentWindow )
        {
            return;
        }
        ImGuiWindow* window = context->CurrentWindow;
        ImGuiWindow* root = window->RootWindow ? window->RootWindow : window;
        WindowState& windowState = Get().Windows[root->ID];
        windowState.Group.clear();
        windowState.Sections.clear();
    }

    void SetLastItemLabel( std::string_view InLabel )
    {
        ImGuiContext* context = ImGui::GetCurrentContext();
        if( !Get().Enabled || !context )
        {
            return;
        }
        if( Item* item = FindBuilding( context->LastItemData.ID ) )
        {
            item->Label = std::string( InLabel );
        }
    }

    void SetLastItemValue( std::string_view InValue )
    {
        if( ImGuiContext* context = ImGui::GetCurrentContext() )
        {
            SetItemValue( context->LastItemData.ID, InValue );
        }
    }

    const std::vector<Item>& LastFrame()
    {
        return Get().Last;
    }

    std::string VisibleLabel( std::string_view InLabel )
    {
        const size_t hashes = InLabel.find( "##" );
        return std::string( Trim( hashes == std::string_view::npos ? InLabel : InLabel.substr( 0, hashes ) ) );
    }

    Query Parse( std::string_view InQuery )
    {
        Query query;
        std::string_view text = Trim( InQuery );
        if( text.size() > 2 && text[0] == '0' && ( text[1] == 'x' || text[1] == 'X' ) )
        {
            query.Id = static_cast<ImGuiID>( std::strtoul( std::string( text.substr( 2 ) ).c_str(), nullptr, 16 ) );
            return query;
        }
        const size_t at = text.rfind( '@' );
        if( at != std::string_view::npos && at + 1 < text.size() && std::all_of( text.begin() + at + 1, text.end(), []( char c ) { return std::isdigit( static_cast<unsigned char>( c ) ); } ) )
        {
            query.Index = std::max( 1, std::atoi( std::string( text.substr( at + 1 ) ).c_str() ) );
            text = text.substr( 0, at );
        }
        constexpr std::string_view kLabelSuffix = ":label";
        if( text.size() > kLabelSuffix.size() && EqualsNoCase( text.substr( text.size() - kLabelSuffix.size() ), kLabelSuffix ) )
        {
            query.WantLabel = true;
            text.remove_suffix( kLabelSuffix.size() );
        }
        size_t start = 0;
        while( true )
        {
            const size_t slash = text.find( '/', start );
            const std::string_view part = Trim( text.substr( start, slash == std::string_view::npos ? std::string_view::npos : slash - start ) );
            if( slash == std::string_view::npos )
            {
                query.Name = std::string( part );
                break;
            }
            if( !part.empty() )
            {
                query.Scopes.emplace_back( part );
            }
            start = slash + 1;
        }
        return query;
    }

    bool Matches( const Item& InItem, const Query& InQuery )
    {
        if( InQuery.Id != 0 )
        {
            return InItem.Id == InQuery.Id;
        }
        if( InItem.IsWindow || InQuery.WantLabel != InItem.IsRowLabel )
        {
            return false;
        }
        const bool named = NameMatches( InItem.Label, InQuery.Name ) || NameMatches( InItem.Row, InQuery.Name );
        if( !named )
        {
            return false;
        }
        for( const std::string& scope : InQuery.Scopes )
        {
            if( !EqualsNoCase( InItem.Window, scope ) && !NameMatches( InItem.Group, scope ) && !NameMatches( InItem.Section, scope )
                && !( !InItem.IsRowLabel && NameMatches( InItem.Row, scope ) ) )
            {
                return false;
            }
        }
        return true;
    }

    std::vector<const Item*> FindAll( std::string_view InQuery )
    {
        const Query query = Parse( InQuery );
        std::vector<const Item*> found;
        for( const Item& item : Get().Last )
        {
            if( Matches( item, query ) )
            {
                found.push_back( &item );
            }
        }
        return found;
    }

    const Item* Find( std::string_view InQuery, std::string& OutError )
    {
        if( !Get().Enabled )
        {
            OutError = "the item log is off";
            return nullptr;
        }
        const Query query = Parse( InQuery );
        const auto found = FindAll( InQuery );
        if( found.size() < static_cast<size_t>( query.Index ) )
        {
            OutError = found.empty() ? "no item matches '" + std::string( InQuery ) + "' (imgui-dump lists them)"
                                     : "only " + std::to_string( found.size() ) + " items match '" + std::string( InQuery ) + "'";
            return nullptr;
        }
        return found[query.Index - 1];
    }

    std::string Describe( const Item& InItem )
    {
        char rect[96];
        std::snprintf( rect, sizeof( rect ), "0x%08X [%.0f,%.0f %.0fx%.0f]%s", InItem.Id, InItem.Rect.Min.x, InItem.Rect.Min.y,
            InItem.Rect.GetWidth(), InItem.Rect.GetHeight(), ( InItem.Flags & ImGuiItemStatusFlags_Visible ) ? "" : " clipped" );
        std::string text = std::string( rect ) + " " + InItem.Window;
        if( !InItem.Group.empty() && InItem.Group != InItem.Section ) text += " / " + InItem.Group;
        if( !InItem.Section.empty() ) text += " / " + InItem.Section;
        text += InItem.IsRowLabel ? " / " + InItem.Label + ":label" : " / " + ( InItem.Label.empty() ? std::string( "<unlabelled>" ) : InItem.Label );
        if( !InItem.Row.empty() && !InItem.IsRowLabel ) text += " (row " + InItem.Row + ")";
        if( !InItem.Value.empty() ) text += " = " + InItem.Value;
        return text;
    }
}

// ImGui's test-engine hooks (imgui_internal.h): ImGui calls these for every item while
// ImGuiContext::TestEngineHookItems is set.
void ImGuiTestEngineHook_ItemAdd( ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* )
{
    MeWidgets::ItemLog::Record( ctx, id, bb );
}

void ImGuiTestEngineHook_ItemInfo( ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags )
{
    MeWidgets::ItemLog::Describe( ctx, id, label, flags );
}

void ImGuiTestEngineHook_Log( ImGuiContext*, const char*, ... )
{
}

const char* ImGuiTestEngine_FindItemDebugLabel( ImGuiContext*, ImGuiID id )
{
    for( const auto& item : MeWidgets::ItemLog::LastFrame() )
    {
        if( item.Id == id )
        {
            return item.Label.c_str();
        }
    }
    return nullptr;
}
