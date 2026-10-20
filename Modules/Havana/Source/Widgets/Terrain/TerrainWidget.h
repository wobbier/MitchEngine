#pragma once
#include "Dementia.h"
#if USING( ME_EDITOR )
#include "HavanaWidget.h"
#include "Events/EventReceiver.h"
#include "TerrainDocument.h"
#include "Math/Vector2.h"
#include "Terrain/TerrainHeightmap.h"
#include "Foliage/FoliageData.h"
#include <bgfx/bgfx.h>
#include <functional>
#include <map>
#include <set>
#include <future>

class SceneViewWidget;
class TerrainWidget : public HavanaWidget, public EventReceiver
{
public:
    TerrainWidget();
    ~TerrainWidget();
    static TerrainWidget* Get();
    void Init() override;
    void Destroy() override;
    void Update() override {}
    void Render() override;
    void InstallActionRouting();
    TerrainDocument* Open( const std::string& InPath );
    TerrainDocument* Create( const std::string& InPath, const TerrainData::Settings& InSettings, float InHeight = 0 );
    TerrainDocument* CreateFromField( const std::string& InPath, TerrainData::Heightfield InField );
    bool MakeUnique( const std::string& InPath, uint64_t InEntityGUID = 0 );
    TerrainDocument* GetActive() const { return m_active; }
    bool HasUnsaved() const;
    bool CanMoveAssets( const std::string& InFrom, const std::string& InTo, std::string& OutError ) const;
    bool CanRemoveAssets( const std::string& InPath, std::string& OutError ) const;
    void OnAssetsMoved( const std::string& InFrom, const std::string& InTo, const std::string& InBefore = {}, const std::string& InAfter = {} );
    void OnAssetsRemoved( const std::string& InPath );
    bool SaveAll();
    bool ConfigureRecovery( const std::filesystem::path& InDirectory );
    bool WriteRecoveries();
    void ScanRecoveries();
    bool RestoreRecovery( size_t InIndex );
    bool RestoreAssetRecovery( const std::string& InAssetPath );
    bool DiscardRecovery( size_t InIndex );
    size_t GetRecoveryCount() const { return m_recoveries.size(); }
    void PrepareForPlay();
    void RequestStartupRecovery() { m_showRecoveryRequested = !m_recoveries.empty(); }
    void ConfirmUnsaved( std::function<void()> InAction );
    // Drawn from the world viewport. True owns the entire gesture (gizmos and picking abstain).
    bool DrawViewport( SceneViewWidget& InView, const Vector2& InScreenMin, const Vector2& InScreenSize );
    bool WantsViewport() const;
    bool AddFoliageModel( const std::string& InPath );
    bool AddFoliageStarters();
    bool BeginFoliageStroke( double InX, double InZ, bool InReverse = false );
    bool MoveFoliageStroke( double InX, double InZ );
    void EndFoliageStroke( bool InCancel = false );
    bool ClearFoliage( bool InAll );
    bool ReprojectFoliage();
    Foliage::Brush FoliageBrush;
    void SetFoliageBrush( const Foliage::Brush& InBrush, bool InRecord = true );
    void SetCategory( int InCategory )
    {
        EndFoliageStroke();
        if( m_active ) m_active->EndStroke();
        m_category = InCategory >= 0 && InCategory < 6 ? InCategory : 0;
        if( m_category == 1 && Brush.Tool < TerrainData::BrushTool::Paint ) Brush.Tool = TerrainData::BrushTool::Paint;
        if( m_category == 0 && Brush.Tool >= TerrainData::BrushTool::Paint ) Brush.Tool = TerrainData::BrushTool::Raise;
    }
    bool FillLayer( unsigned InLayer );
    void EditSurfaceLayer();
    Vector2 GetLayerMapCenter( unsigned InKind ) const { return m_layerMapCenters[InKind]; }
    Vector2 GetLayerApplyCenter() const { return m_layerApplyCenter; }
    enum class BrushParameter { Radius, Strength, Falloff, Spacing };
    void SetBrushParameter( BrushParameter InParameter, float InValue, bool InRecord = true );
    Vector2 GetBrushControlCenter( BrushParameter InParameter ) const { return m_brushCenters[static_cast<int>( InParameter )]; }
    Vector2 GetLodButtonCenter( bool InPatches ) const { return m_lodButtonCenters[InPatches ? 0 : 1]; }
    UndoStack& GetHistory() { return m_category == 3 ? UndoStack::Get() : m_active ? m_active->GetUndo() : m_brushHistory; }
    TerrainData::BrushSettings Brush;
    bool BrushEnabled = false;
    bool StartStreamCook( const std::string& InPath, uint8_t InCoverageLod, bool InPaged = true, bool InCompressed = false, bool InHierarchical = false );
    bool FinishStreamCook( bool InWait = false );
    bool IsCooking() const { return m_cook.valid(); }

private:
    bool ClaimsAction( const std::string& InId ) const;
    bool IsPlaying() const;
    void NewDialog();
    void ImportDialog();
    void ExportDialog();
    void MakeUniqueDialog();
    void DrawModals();
    void InitRecovery();
    void UpdateRecovery();
    void DrawRecovery();
    bool AutomaticRecoveryEnabled() const;
    void DrawBrush();
    void DrawPaint();
    void DrawFoliage();
    bool DrawFoliageViewport( SceneViewWidget& InView, const Vector2& InScreenMin, const Vector2& InScreenSize );
    Foliage::Stroke m_foliageStroke;
    Foliage::Data m_foliageBefore;
    Foliage::Brush m_foliageControlBefore;
    uint64_t m_foliageEntity = 0;
    void DrawLayerMap( unsigned InKind, const char* InLabel, std::string& InPath );
    void RecordBrushParameter( BrushParameter InParameter, float InBefore );
    void DrawTools();
    void DrawLodButtons();
    void DrawLodControls();
    void DrawStreamingControls();
    void DrawResidencyButton();
    void StreamCookDialog();
    void DrawStreamCook();
    void PlaceInScene();
    bgfx::TextureHandle Icon( const char* InName );
    bool Button( const char* InIcon, const char* InLabel, const char* InDescription, bool InEnabled = true, bool InSelected = false, bool InCompact = false );
    void Planned( const char* InIcon, const char* InLabel, const char* InDescription );

    std::vector<std::unique_ptr<TerrainDocument>> m_documents;
    TerrainDocument* m_active = nullptr;
    TerrainDocument* m_revertTarget = nullptr;
    std::map<std::string, bgfx::TextureHandle> m_icons;
    std::function<void()> m_pending;
    bool m_focusRequested = false, m_focused = false, m_viewportOwnsActions = false;
    bool m_newRequested = false, m_confirmRequested = false;
    bool m_importHeightmap = false, m_exportRequested = false;
    std::string m_rawPath;
    TerrainData::RawHeightmap m_rawImport, m_rawExport;
    int m_category = 0;
    TerrainData::SurfaceLayer m_layerDraft;
    bool m_layerEditRequested = false;
    uint64_t m_layerEditGeneration = 0;
    Vector2 m_layerMapCenters[3], m_layerApplyCenter;
    unsigned m_layerIndex = 0;
    TerrainDocument* m_layerDocument = nullptr;
    int m_tiles[2] = { 2, 2 }, m_quads = 32;
    float m_spacing = 1, m_flatHeight = 0;
    std::string m_error;
    UndoStack m_brushHistory;
    float m_brushBefore = 0;
    Vector2 m_brushCenters[4];
    Vector2 m_lodButtonCenters[2];
    std::future<std::string> m_cook;
    std::string m_cookPath, m_cookMessage;
    int m_coverageLod = 3;
    bool m_cookRequested = false, m_replaceCook = false;
    bool m_pagedCook = true;
    bool m_compressedCook = true;
    bool m_hierarchicalCook = false;
    uint64_t m_strokeEntity = 0;
    double m_lastPreviewTime = 0;
    struct RecoveryEntry
    {
        std::filesystem::path File;
        TerrainData::RecoveryInfo Info;
        std::string Error;
    };
    std::filesystem::path m_recoveryDirectory;
    std::vector<RecoveryEntry> m_recoveries;
    uint64_t m_recoverySession = 0;
    double m_nextRecoveryAt = 0;
    bool m_showRecoveryRequested = false;
    std::string m_recoveryMessage;
    std::set<TerrainDocument*> m_recoveryDirtyDocuments;
};
#endif
