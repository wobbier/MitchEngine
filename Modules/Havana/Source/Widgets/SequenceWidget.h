#pragma once
#include "HavanaWidget.h"
#include "ECS/EntityHandle.h"
#include "JSON.h"
#include "Events/EventReceiver.h"
#include "SequenceTimeline.h"
#include <functional>
#include <memory>
#include <unordered_map>

namespace Moonlight { class Texture; }
class World;

#if USING( ME_EDITOR )
class SequenceDocument;
class SequenceWidget final : public HavanaWidget, public EventReceiver
{
public:
    SequenceWidget();
    ~SequenceWidget();
    static SequenceWidget* Get();
    void Init() override;
    void Destroy() override;
    void Update() override {}
    void Render() override;
    void InstallActionRouting();
    bool OpenAsset( const std::string& InPath );
    bool ExportAsset( const std::string& InPath );
    bool HasUnsaved() const;
    void ConfirmUnsaved( std::function<void()> InAction );
    bool ExecuteAutomation( const std::string& InCommand, const std::string& InArgs, std::string& OutError );
private:
    EntityHandle m_owner;
    EntityHandle m_target;
    int m_frame = 0;
    int m_track = -1;
    SequenceTimeline m_timeline;
    std::unique_ptr<SequenceDocument> m_document;
    std::unique_ptr<Sequence> m_standalone;
    std::function<void()> m_pending;
    bool m_openUnsaved = false;
    bool m_focused = false;
    bool m_focusRequested = false;
    uint64_t m_selectionVersion = 0;
    std::string m_error;
    std::string m_assignRequested;
    std::unordered_map<std::string, SharedPtr<Moonlight::Texture>> m_icons;
    json m_editBefore;
    Sequence* Working() const;
    bool IsAsset() const;
    json EditState() const;
    void CommitEdit( const json& InBefore );
    void FinishEdit();
    void SetOwner( EntityHandle InOwner );
    void DrawBody();
    void DrawTrackInspector( Sequence& InSequence, EntityHandle InSelected );
    void DrawPlayerInspector( Sequence& InSequence, World& InWorld );
    void DrawClipLibrary( Sequence& InSequence );
    void DrawEventInspector( Sequence& InSequence, EntityHandle InSelected );
    void SetMarkerReceiver( Sequence& InSequence, SequenceMarker& InMarker, EntityHandle InTarget );
    int m_shownMarker = -1;
    void DrawCutInspector( Sequence& InSequence, EntityHandle InSelected );
    void SetCutCamera( Sequence& InSequence, SequenceCut& InCut, EntityHandle InCamera );
    int m_shownCut = -1;
    void AssignAsset( const std::string& InPath );
    bool TransportButton( const char* InIcon, const char* InTooltip );
    void DrawUnsaved();
    void ExportDialog();
    bool ClaimsAction( const std::string& InId ) const;
    void ExecuteAction( const std::string& InId );
};
#endif
