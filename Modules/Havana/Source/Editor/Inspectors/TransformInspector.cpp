#include "TransformInspector.h"

#if USING( ME_EDITOR )
#include "Editor/ReflectionUI.h"
#include "Components/Transform.h"
#include <Utils/EditorConfig.h>
#include <Widgets/TransformWidgets.h>

namespace TransformInspector
{
    namespace
    {
        constexpr const char* kScaleLock = "Inspector.ScaleLock";
        constexpr const char* kRotationDials = "Inspector.RotationDials";

        // The Euler angles a dial drag started from, so moving one dial never makes the others
        // jump through a quaternion round trip.
        struct DialDrag
        {
            const void* Instance = nullptr;
            Vector3 Euler;
        };

        DialDrag& Drag()
        {
            static DialDrag drag;
            return drag;
        }
    }

    void Register()
    {
        ReflectionUI::SetVectorRowOptions( "Transform", "Position", { "", "", true } );
        ReflectionUI::SetVectorRowOptions( "Transform", "Scale", { kScaleLock, "" } );
        ReflectionUI::SetVectorRowOptions( "Transform", "Rotation", { "", kRotationDials } );

        ReflectionUI::RegisterFieldDecorator( "Transform", "Rotation", { "Rotation" }, []( void* InInstance, const Reflection::TypeInfo& ) {
            if( !EditorConfig::GetInstance().GetPreference<bool>( kRotationDials, false ) )
            {
                return false;
            }
            Transform& transform = *static_cast<Transform*>( static_cast<BaseComponent*>( InInstance ) );
            DialDrag& drag = Drag();
            Vector3 euler = drag.Instance == InInstance ? drag.Euler : transform.GetRotationEuler();
            bool active = false;
            const bool changed = MeWidgets::AngleDials( "Dials", &euler.x, 3, &active );
            drag.Instance = active ? InInstance : nullptr;
            drag.Euler = euler;
            if( changed )
            {
                transform.SetRotation( euler );
            }
            return changed;
        } );
    }
}

#endif
