#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )
#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "Editor/UndoStack.h"
#include "JSON.h"
#include "Math/Vector2.h"
#include "ShaderGraph/Compiler.h"
#include "ShaderGraph/Graph.h"
#include "imgui.h"
#include <bgfx/bgfx.h>

class ShaderGraphPreview;

namespace ax::NodeEditor { struct EditorContext; }
namespace ShaderGraph { class NodeDef; struct PinDef; }

// One open .shadergraph: its canvas, blackboard, inspector and output panels, its own undo
// history, and live compilation (every edit recompiles; successful compiles are exported so the
// scene's materials follow along).
class ShaderGraphDocument
{
public:
    // InPath: project path ("Assets/Foo.shadergraph").
    explicit ShaderGraphDocument( const std::string& InPath );
    ~ShaderGraphDocument();

    bool Load( std::string* OutError = nullptr );
    // Writes the graph and exports its generated shaders.
    bool Save();

    // Draws the document window. Returns false once it has been closed (after the unsaved-changes
    // prompt, if any).
    bool Draw();
    // Renders the node previews and the main preview into two offscreen views (before Draw).
    void RenderPreviews( bgfx::ViewId InTileView, bgfx::ViewId InMainView );
    ShaderGraphPreview& GetPreview()
    {
        return *m_preview;
    }
    void RequestFocus();
    // Fits the whole graph in the canvas on the next draw.
    void RequestFrameAll()
    {
        m_frameOnNextDraw = true;
    }
    // The window had focus last frame (it owns Save / Undo / Copy / Delete then).
    bool IsFocused() const
    {
        return m_isFocused;
    }
    bool IsDirty() const;
    const std::string& GetPath() const
    {
        return m_path;
    }

    bool ClaimsAction( const std::string& InId ) const;
    void ExecuteAction( const std::string& InId );

    // Editing (the UI and editor automation use these): each call is one undo step.
    uint32_t AddNode( const std::string& InType, const Vector2& InPosition, const json& InSettings = json::object() );
    void RemoveNodes( const std::vector<uint32_t>& InIds );
    bool Connect( uint32_t InFromNode, const std::string& InFromPin, uint32_t InToNode, const std::string& InToPin, std::string* OutError = nullptr );
    void Disconnect( uint32_t InToNode, const std::string& InToPin );
    void SetNodeSetting( uint32_t InNode, const std::string& InSetting, const json& InValue );
    void SetInputDefault( uint32_t InNode, const std::string& InPin, const Vector4& InValue );
    std::string AddProperty( const std::string& InName, ShaderGraph::ValueType InType );
    void SetTarget( ShaderGraph::Target InTarget );

    ShaderGraph::Graph& GetGraph()
    {
        return m_graph;
    }
    // The last codegen result (diagnostics, resolved pin types, generated sources).
    const ShaderGraph::CompileResult& GetCompileResult() const
    {
        return m_result;
    }
    // Compiles now (normally debounced after edits) and exports when live.
    void CompileNow();
    uint32_t GetCompileCount() const
    {
        return m_compileCount;
    }
    UndoStack& GetUndo()
    {
        return m_undo;
    }

    // Undo snapshots.
    void ApplySnapshot( const json& InSnapshot );

private:
    friend class GraphEditScope;

    // Opens an edit: the graph's state now is what Undo restores. Edits with the same non-empty
    // InMergeKey during one widget interaction (a drag) become a single undo step.
    void BeginEdit( const std::string& InName, const std::string& InMergeKey = "" );
    void EndEdit();

    void DrawToolbar();
    void DrawSidePanel( float InWidth );
    void DrawMainPreview( float InWidth );
    void DrawBlackboard();
    void DrawGraphSettings();
    void DrawNodeInspector();
    void DrawCanvas();
    void DrawNode( ShaderGraph::Node& InNode );
    void DrawGroups();
    void DrawMessages();
    void DrawCodeWindow();
    void DrawPalette();
    void DrawContextMenus();
    void DrawClosePrompt();
    bool DrawInlineValue( ShaderGraph::Node& InNode, const ShaderGraph::PinDef& InPin, ShaderGraph::ValueType InType, float InWidth );
    bool DrawSettingEditor( ShaderGraph::Node& InNode, const ShaderGraph::NodeDef& InDef, size_t InSettingIndex, bool InCompact );

    void OpenPalette( const ImVec2& InCanvasPosition, uint64_t InFromPin );
    void HandleCreateAndDelete();
    void SyncNodePositions();
    void CopySelection( bool InCut );
    void Paste( const ImVec2& InCanvasPosition );
    void DuplicateSelection();
    void DeleteSelection();
    void FrameSelection( bool InAll );
    std::vector<uint32_t> GetSelectedNodes() const;

    // Pin ids for the node editor: node id << 20 | pin index << 1 | is-output.
    static uint64_t MakePinId( uint32_t InNode, size_t InIndex, bool InIsOutput );
    bool DecodePin( uint64_t InPinId, uint32_t& OutNode, std::string& OutPin, bool& OutIsOutput ) const;
    ShaderGraph::ValueType GetPinType( uint32_t InNode, const std::string& InPin, bool InIsOutput ) const;
    bool WouldCreateLoop( uint32_t InFromNode, uint32_t InToNode ) const;
    bool CanConnect( uint32_t InFromNode, const std::string& InFromPin, uint32_t InToNode, const std::string& InToPin, std::string* OutReason ) const;
    std::vector<std::string> GetNodeErrors( uint32_t InNode ) const;

    std::string m_path;
    std::string m_fullPath;
    std::string m_title;
    ShaderGraph::Graph m_graph;
    ShaderGraph::CompileResult m_result;
    UndoStack m_undo;
    ax::NodeEditor::EditorContext* m_editor = nullptr;
    std::unique_ptr<ShaderGraphPreview> m_preview;
    bool m_isVisible = false;

    // Edit in progress.
    json m_editBefore;
    std::string m_editName;
    std::string m_editMergeKey;
    int m_editDepth = 0;

    // Compilation.
    bool m_needsCompile = true;
    std::chrono::steady_clock::time_point m_lastEdit;
    bool m_liveExport = true;
    bool m_exportPending = false;
    uint32_t m_compileCount = 0;

    // Nodes whose canvas position must be (re)applied (new nodes, undo).
    std::set<uint32_t> m_positionDirty;
    bool m_allPositionsDirty = true;
    bool m_frameOnNextDraw = true;

    // UI state.
    bool m_isOpen = true;
    bool m_isFocused = false;
    bool m_focusRequested = true;
    bool m_showClosePrompt = false;
    bool m_showCode = false;
    float m_sidePanelWidth = 300.f;
    uint32_t m_inspectedNode = 0;
    bool m_focusNodeInspector = false;
    std::string m_inspectedProperty;
    std::string m_renamingProperty;
    char m_renameBuffer[128] = {};

    // Node palette.
    bool m_paletteRequested = false;
    ImVec2 m_paletteCanvasPosition;
    uint64_t m_paletteFromPin = 0;
    char m_paletteFilter[128] = {};
    int m_paletteSelection = 0;

    // Context menus (opened between ed::Suspend/Resume).
    uint32_t m_contextNode = 0;
    uint64_t m_contextLink = 0;
    // Enum popups opened from inside a node.
    uint32_t m_enumPopupNode = 0;
    std::string m_enumPopupSetting;
    bool m_enumPopupRequested = false;
};
#endif
