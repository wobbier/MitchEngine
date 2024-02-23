#include "Pool.h"

Pool::Pool( std::size_t InMaxJobs )
    : AllocatedJobs( 0 )
    , Storage { InMaxJobs }
{
}

Job* Pool::Allocate()
{
    if( !IsFull() )
    {
        return &Storage[AllocatedJobs++];
    }
    return nullptr;
}

bool Pool::IsFull() const
{
    return AllocatedJobs == Storage.size();
}

void Pool::Clear()
{
    AllocatedJobs = 0;
}

Job* Pool::CreateJob( JobFunc InJobFunc )
{
    Job* job = Allocate();

    if( job )
    {
        new( job ) Job { InJobFunc };
        return job;
    }

    return nullptr;
}

Job* Pool::CreateJobAsChild( JobFunc InJobFunc, Job* InParent )
{

    return nullptr;
}

