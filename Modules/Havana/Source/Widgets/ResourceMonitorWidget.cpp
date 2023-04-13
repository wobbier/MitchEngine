#include "Widgets/ResourceMonitorWidget.h"
#include <optick.h>
#include <Resource/ResourceCache.h>
#include <vector>

#if USING( ME_EDITOR )

const ImGuiTableSortSpecs* ResourceMonitorWidget::s_SortSpecs = nullptr;

ResourceMonitorWidget::ResourceMonitorWidget()
    : HavanaWidget( "Resource Monitor" )
{
}

void ResourceMonitorWidget::Init()
{
}

void ResourceMonitorWidget::Destroy()
{
}

void ResourceMonitorWidget::Update()
{
}

void ResourceMonitorWidget::Render()
{
    if( IsOpen )
    {
        OPTICK_CATEGORY( "Resource Monitor", Optick::Category::UI );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.f, 0.f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 12.f, ImGui::GetStyle().FramePadding.y ) );
        ImGui::Begin( "Resource Monitor", &IsOpen );
        {
            ResourceStack& resources = ResourceCache::GetInstance().GetResouceStack();

            if( ItemsNeedGenerated )
            {
                resourceList.clear();
                resourceList.reserve( resources.size() );

                for( auto& resource : resources )
                {
                    if( resource.second )
                    {
                        resourceList.push_back( resource.second );
                    }
                }
                ItemsNeedSorted = true;
                ItemsNeedGenerated = false;
            }

            ImVec2 size = ImVec2( ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y );
            static ImGuiTableFlags flags =
                ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable
                | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti
                | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody
                | ImGuiTableFlags_ScrollY
                | ImGuiTableFlags_SizingStretchProp;

            if( ImGui::BeginTable( "##ResourceTable", 2, flags, size ) )
            {
                ImGui::TableSetupColumn( "Resource Path", ImGuiTableColumnFlags_WidthStretch, -1.f, Sort_LocalPath );
                ImGui::TableSetupColumn( "References", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 15.f, Sort_RefCount );
                ImGui::TableSetupScrollFreeze( 1, 1 );
                ImGui::TableHeadersRow();

                ImGuiTableSortSpecs* sorts_specs = ImGui::TableGetSortSpecs();
                if( sorts_specs && sorts_specs->SpecsDirty )
                {
                    ItemsNeedSorted = true;
                }
                if( sorts_specs && ItemsNeedSorted && resourceList.size() > 1 )
                {
                    s_SortSpecs = sorts_specs; // Store in variable accessible by the sort function.
                    qsort( &resourceList[0], (size_t)resourceList.size(), sizeof( resourceList[0] ), CompareWithSortSpecs );
                    s_SortSpecs = NULL;
                    sorts_specs->SpecsDirty = false;
                }
                ItemsNeedSorted = false;

                ImGuiListClipper clipper;
                clipper.Begin( static_cast<int>( resourceList.size() ), 17.f );
                OPTICK_CATEGORY( "List Clipper", Optick::Category::UI );
                while( clipper.Step() )
                {
                    for( int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++ )
                    {
                        OPTICK_EVENT( "Table Entry" );
                        {
                            if( resourceList[row].expired() )
                            {
                                ItemsNeedGenerated = true;
                                continue;
                            }

                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex( 0 );
                            {
                                OPTICK_EVENT( "Name" );
                                ImGui::Text( resourceList[row].lock()->GetPath().LocalPath.c_str() );
                            }

                            ImGui::TableSetColumnIndex( 1 );
                            {
                                OPTICK_EVENT( "Use Count" );
                                // I'm negating 1 here for the actual pointer stored in the ResourceCache
                                ImGui::Text( std::to_string( resourceList[row].use_count() - 1 ).c_str() );
                            }
                        }

                    }
                }
                ImGui::EndTable();
            }
            ImGui::End();
        }
        ImGui::PopStyleVar( 2 );
    }
}

int ResourceMonitorWidget::CompareWithSortSpecs( const void* lhs, const void* rhs )
{
    const SharedPtr<Resource> a = ( *( WeakPtr<Resource>* )lhs ).lock();
    const SharedPtr<Resource> b = ( *( WeakPtr<Resource>* )rhs ).lock();
    for( int n = 0; n < s_SortSpecs->SpecsCount; n++ )
    {
        // Here we identify columns using the ColumnUserID value that we ourselves passed to TableSetupColumn()
        // We could also choose to identify columns based on their index (sort_spec->ColumnIndex), which is simpler!
        const ImGuiTableColumnSortSpecs* sort_spec = &s_SortSpecs->Specs[n];
        int delta = 0;
        switch( sort_spec->ColumnUserID )
        {
        case Sort_LocalPath: delta = ( strcmp( a->GetPath().LocalPath.c_str(), b->GetPath().LocalPath.c_str() ) );     break;
        case Sort_RefCount:  delta = ( a.use_count() - b.use_count() );    break;
        default: IM_ASSERT( 0 ); break;
        }
        if( delta > 0 )
            return ( sort_spec->SortDirection == ImGuiSortDirection_Ascending ) ? +1 : -1;
        if( delta < 0 )
            return ( sort_spec->SortDirection == ImGuiSortDirection_Ascending ) ? -1 : +1;
    }

    // qsort() is instable so always return a way to differenciate items.
    // Your own compare function may want to avoid fallback on implicit sort specs e.g. a Name compare if it wasn't already part of the sort specs.
    return ( a.use_count() - b.use_count() );
}

#endif
