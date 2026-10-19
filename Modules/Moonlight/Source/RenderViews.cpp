#include "RenderViews.h"
#include "CLog.h"

namespace Moonlight
{
    bgfx::ViewId ViewAllocator::Allocate( const char* InName )
    {
        if( m_next > RenderView::DynamicLast )
        {
            if( !m_exhausted )
            {
                YIKES( "Out of render views this frame; extra passes are skipped. Fewer cameras or effects needed." );
                m_exhausted = true;
            }
            return UINT16_MAX;
        }
        const bgfx::ViewId id = static_cast<bgfx::ViewId>( m_next++ );
        bgfx::resetView( id );
        bgfx::setViewName( id, InName );
        if( m_names.size() <= id ) m_names.resize( id + 1 );
        m_names[id] = InName;
        return id;
    }


    const std::string& ViewAllocator::GetName( bgfx::ViewId InId ) const
    {
        static const std::string kNone;
        return InId < m_names.size() && InId >= RenderView::DynamicFirst && InId < m_next ? m_names[InId] : kNone;
    }
}
