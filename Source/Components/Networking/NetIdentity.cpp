#include "PCH.h"
#include "Components/Networking/NetIdentity.h"

#if USING( ME_EDITOR )
#include <imgui.h>
#endif

ME_REFLECT_BEGIN( NetIdentity )
    ME_FIELD( Prefab ).Asset( "Prefab" ).Tooltip( "What clients create for a copy spawned while the game runs (empty: a bare entity)" );
ME_REFLECT_END()


NetIdentity::NetIdentity()
    : Component( "NetIdentity" )
{
}


#if USING( ME_EDITOR )
void NetIdentity::OnEditorInspect()
{
    Component::OnEditorInspect();
    if( m_netId != 0 )
    {
        ImGui::Text( "NetId %u, owner %u", m_netId, m_owner );
    }
}
#endif
