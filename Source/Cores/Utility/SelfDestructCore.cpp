#include "PCH.h"
#include "SelfDestructCore.h"

void SelfDestructor::Update( const UpdateContext& context )
{
    auto entities = GetEntities();
    for ( Entity& entity : entities )
    {
        SelfDestruct& destruct = entity.GetComponent<SelfDestruct>();
        destruct.Lifetime -= context.GetDeltaTime();
        if ( destruct.Lifetime <= 0 )
        {
            entity.MarkForDelete();
        }
    }
}
