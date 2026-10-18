#include "TerrainWidget.h"
#if USING( ME_EDITOR )
#include "Utils/EditorConfig.h"
#include <algorithm>
#include <cmath>

namespace
{
    const char* Names[] = { "Radius", "Strength", "Falloff", "Spacing" };
    float& Value( TerrainData::BrushSettings& brush, TerrainWidget::BrushParameter parameter )
    {
        switch( parameter )
        {
        case TerrainWidget::BrushParameter::Radius: return brush.Radius;
        case TerrainWidget::BrushParameter::Strength: return brush.Strength;
        case TerrainWidget::BrushParameter::Falloff: return brush.Falloff;
        default: return brush.Spacing;
        }
    }

    class BrushCommand final : public UndoCommand
    {
    public:
        BrushCommand( TerrainWidget& widget, TerrainWidget::BrushParameter parameter, float before, float after )
            : Widget( widget ), Parameter( parameter ), Before( before ), After( after ) {}
        void Undo() override { Widget.SetBrushParameter( Parameter, Before, false ); }
        void Redo() override { Widget.SetBrushParameter( Parameter, After, false ); }
        std::string GetName() const override { return std::string( "Brush " ) + Names[static_cast<int>( Parameter )]; }
        bool AffectsSavedState() const override { return false; }
    private:
        TerrainWidget& Widget; // The widget owns every document/history containing these commands.
        TerrainWidget::BrushParameter Parameter;
        float Before, After;
    };
}


void TerrainWidget::SetBrushParameter( BrushParameter parameter, float value, bool record )
{
    if( IsPlaying() || ( m_active && m_active->IsStroking() ) || !std::isfinite( value ) ) return;
    const float spacing = m_active ? m_active->GetField().GetSettings().SampleSpacing : 1.f;
    const float minimum[] = { spacing * 0.25f, 0.001f, 0.f, 0.05f };
    const float maximum[] = { spacing * 128.f, 100.f, 1.f, 1.f };
    const int index = static_cast<int>( parameter );
    if( index < 0 || index >= 4 ) return;
    float& target = Value( Brush, parameter );
    const float before = target;
    // UI values are already constrained. Undo must also restore a prior preference inherited
    // from another document, even if that document used a different sample spacing.
    target = record ? std::clamp( value, minimum[index], maximum[index] ) : value;
    EditorConfig::GetInstance().SetPreference( std::string( "Terrain." ) + Names[index], target );
    if( record ) RecordBrushParameter( parameter, before );
}


void TerrainWidget::RecordBrushParameter( BrushParameter parameter, float before )
{
    const float after = Value( Brush, parameter );
    if( before != after ) GetHistory().Push( std::make_unique<BrushCommand>( *this, parameter, before, after ) );
}
#endif
