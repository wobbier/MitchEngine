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
        return id;
    }
}
