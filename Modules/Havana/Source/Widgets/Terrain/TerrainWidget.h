#pragma once
#include "Dementia.h"
#if USING( ME_EDITOR )
#include "HavanaWidget.h"
#include "Events/EventReceiver.h"
#include "TerrainDocument.h"
#include "Math/Vector2.h"
#include "Terrain/TerrainHeightmap.h"
#include <bgfx/bgfx.h>
#include <functional>
#include <map>

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
    bool SaveAll();
    void ConfirmUnsaved( std::function<void()> InAction );
    // Drawn from the world viewport. True owns the entire gesture (gizmos and picking abstain).
    bool DrawViewport( SceneViewWidget& InView, const Vector2& InScreenMin, const Vector2& InScreenSize );
    bool WantsViewport() const;
    void SetCategory( int InCategory ) { m_category = InCategory >= 0 && InCategory < 6 ? InCategory : 0; }
    TerrainData::BrushSettings Brush;
    bool BrushEnabled = false;

private:
    bool ClaimsAction( const std::string& InId ) const;
    bool IsPlaying() const;
    void NewDialog();
    void ImportDialog();
    void ExportDialog();
    void MakeUniqueDialog();
    void DrawModals();
    void DrawBrush();
    void DrawTools();
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
    int m_tiles[2] = { 2, 2 }, m_quads = 32;
    float m_spacing = 1, m_flatHeight = 0;
    std::string m_error;
    uint64_t m_strokeEntity = 0;
    double m_lastPreviewTime = 0;
};
#endif
