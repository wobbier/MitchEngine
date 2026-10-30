#pragma once
#include "Dementia.h"
#if USING( ME_EDITOR )
#include "Animation/SequenceEditing.h"
#include <imgui.h>

// A keyframe canvas with per-window interaction state. The selection/editing model is independent
// of ImGui, shared by pointer gestures, commands and tests.
class SequenceTimeline
{
public:
    bool Draw( Sequence& InSequence, int& InOutFrame, int& InOutTrack );
    void Reset();
    bool Move( Sequence& InSequence, int InDelta );
    bool Delete( Sequence& InSequence );
    void SelectBox( const Sequence& InSequence, float InFirst, float InLast, int InFirstRow, int InLastRow, bool InAdd = false );
    // Event markers (GAME-4) live in their own lane above the tracks; one is selected at a time.
    int AddMarker( Sequence& InSequence, int InFrame );
    bool MoveMarker( Sequence& InSequence, int InIndex, int InFrame );
    bool DeleteMarker( Sequence& InSequence );
    // Camera cuts: one per frame, in the Cameras lane. New cuts use CutCamera (the widget sets it).
    int AddCut( Sequence& InSequence, int InFrame );
    bool MoveCut( Sequence& InSequence, int InIndex, int InFrame );
    bool DeleteCut( Sequence& InSequence );
    EntityHandle CutCamera;
    SequenceEditing::Selection Selected;
    int SelectedMarker = -1;
    int SelectedCut = -1;
    std::string Message;
    // Canvas coordinates also let UI tests drive the same pointer gestures as the user.
    ImVec2 KeyPosition( float InFrame, float InRow ) const;
    ImVec2 MarkerPosition( float InFrame ) const;
    ImVec2 CutPosition( float InFrame ) const;
private:
    float m_firstFrame = 0.f;
    float m_pixelsPerFrame = 0.f;
    bool m_fit = true;
    bool m_box = false, m_drag = false, m_scrub = false;
    bool m_addBox = false;
    bool m_markerDrag = false;
    int m_markerOriginal = 0;
    float m_markerLaneY = 0.f;
    bool m_cutDrag = false;
    int m_cutOriginal = 0;
    float m_cutLaneY = 0.f;
    ImVec2 m_start;
    std::vector<SequenceTrack> m_original;
    SequenceEditing::Selection m_originalSelection;
    int m_snap = 1;
    ImVec2 m_gridOrigin;
    float m_rowHeight = 26.f;
};
#endif
