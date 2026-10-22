#pragma once
#include "ECS/Core.h"

class SequenceCore final : public Core<SequenceCore>
{
public:
    SequenceCore();
    void OnStart() override;
    void OnStop() override;
    void Update( const UpdateContext& InContext ) override;
    void Advance( float InDeltaSeconds );
    void UpdatePreviews( float InDeltaSeconds );
    void StopAllPreviews();
    void RestorePreviewValues();
    bool IsRunning() const { return m_running; }
private:
    bool m_running = false;
};

ME_REGISTER_CORE( SequenceCore )
