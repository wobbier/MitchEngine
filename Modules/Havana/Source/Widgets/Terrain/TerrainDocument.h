#pragma once
#include "Dementia.h"
#if USING( ME_EDITOR )
#include "Editor/UndoStack.h"
#include "Terrain/TerrainEditing.h"
#include "Terrain/ResidentTerrain.h"

// One editable source asset, shared by every scene instance. Scene history is independent.
class TerrainDocument
{
public:
    explicit TerrainDocument( std::string InPath );
    ~TerrainDocument();
    bool Load();
    bool CreateFlat( const TerrainData::Settings& InSettings, float InHeight );
    bool CreateFromField( TerrainData::Heightfield InField );
    bool Save();
    bool SaveCopy( const std::string& InPath );
    bool BeginStroke( const TerrainData::BrushSettings& InBrush, double InX, double InZ );
    bool MoveStroke( double InX, double InZ );
    void EndStroke( bool InCancel = false );
    // Releasing old history must not mark unsaved source samples as saved.
    void DiscardHistory();
    bool PublishPreview();
    void ApplyDeltas( uint64_t InGUID, const std::vector<TerrainData::SampleDelta>& InDeltas, bool InAfter );

    const std::string& GetPath() const { return m_path; }
    const std::string& GetError() const { return m_error; }
    const TerrainData::Heightfield& GetField() const { return m_field; }
    const std::shared_ptr<const TerrainData::ResidentTerrain>& GetPreview() const { return m_preview; }
    UndoStack& GetUndo() { return m_undo; }
    bool IsDirty() const { return m_stroke.IsActive() || m_undo.IsDirty(); }
    bool IsStroking() const { return m_stroke.IsActive(); }
    uint64_t GetGUID() const { return m_guid; }

private:
    std::string m_path, m_manifestText, m_error;
    uint64_t m_guid = 0;
    TerrainData::Heightfield m_field;
    TerrainData::BrushStroke m_stroke;
    std::shared_ptr<const TerrainData::ResidentTerrain> m_preview;
    UndoStack m_undo;
    size_t m_historyBytes = 0;
};
#endif
