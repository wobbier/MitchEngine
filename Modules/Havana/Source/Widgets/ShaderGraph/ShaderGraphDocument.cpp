#include "ShaderGraphDocument.h"
#include "ShaderGraphPreview.h"

#if USING( ME_EDITOR )
#include <algorithm>
#include <cmath>
#include <cstring>
#include "CLog.h"
#include "Events/EditorEvents.h"
#include "Path.h"
#include "ShaderGraph/NodeDef.h"
#include "ShaderGraph/Targets.h"
#include "imgui_internal.h"
#include "imgui_node_editor.h"
#include "misc/cpp/imgui_stdlib.h"
#include "Utils/ImGuiUtils.h"
#include "Utils/HavanaUtils.h"
#include "ImGuiTheme.h"

namespace ed = ax::NodeEditor;
using namespace ShaderGraph;

// RAII form of BeginEdit/EndEdit.
class GraphEditScope
{
public:
    GraphEditScope( ShaderGraphDocument& InDocument, const std::string& InName, const std::string& InMergeKey = "" )
        : m_document( InDocument )
    {
        m_document.BeginEdit( InName, InMergeKey );
    }
    ~GraphEditScope()
    {
        m_document.EndEdit();
    }

private:
    ShaderGraphDocument& m_document;
};

namespace
{
    // One undo step: the whole graph before and after (graphs are small; this keeps every edit,
    // including ones made by automation, trivially reversible).
    class GraphSnapshotCommand
        : public UndoCommand
    {
    public:
        GraphSnapshotCommand( ShaderGraphDocument& InDocument, json InBefore, json InAfter, std::string InName, std::string InMergeKey, ImGuiID InInteraction )
            : m_document( InDocument )
            , m_before( std::move( InBefore ) )
            , m_after( std::move( InAfter ) )
            , m_name( std::move( InName ) )
            , m_mergeKey( std::move( InMergeKey ) )
            , m_interaction( InInteraction )
        {
        }

        void Undo() override
        {
            m_document.ApplySnapshot( m_before );
        }

        void Redo() override
        {
            m_document.ApplySnapshot( m_after );
        }

        std::string GetName() const override
        {
            return m_name;
        }

        bool MergeWith( const UndoCommand& InNext ) override
        {
            const GraphSnapshotCommand* next = dynamic_cast<const GraphSnapshotCommand*>( &InNext );
            if( !next || m_mergeKey.empty() || next->m_mergeKey != m_mergeKey || next->m_interaction != m_interaction || m_interaction == 0 )
            {
                return false;
            }
            m_after = next->m_after;
            return true;
        }

    private:
        ShaderGraphDocument& m_document;
        json m_before;
        json m_after;
        std::string m_name;
        std::string m_mergeKey;
        ImGuiID m_interaction = 0;
    };

    ImColor TypeColor( ValueType InType )
    {
        switch( InType )
        {
        case ValueType::Float:
            return ImColor( 132, 228, 231 );
        case ValueType::Vec2:
            return ImColor( 154, 239, 146 );
        case ValueType::Vec3:
            return ImColor( 246, 255, 154 );
        case ValueType::Vec4:
            return ImColor( 251, 203, 244 );
        case ValueType::Bool:
            return ImColor( 168, 140, 240 );
        case ValueType::Texture2D:
            return ImColor( 255, 128, 128 );
        case ValueType::TextureCube:
            return ImColor( 255, 176, 112 );
        default:
            return ImColor( 190, 190, 190 );
        }
    }

    ImColor CategoryColor( const std::string& InCategory )
    {
        const std::string root = InCategory.substr( 0, InCategory.find( '/' ) );
        if( root == "Input" )
        {
            return ImColor( 70, 125, 62 );
        }
        if( root == "Math" )
        {
            return ImColor( 52, 86, 140 );
        }
        if( root == "Vector" )
        {
            return ImColor( 92, 70, 140 );
        }
        if( root == "Texture" )
        {
            return ImColor( 146, 56, 56 );
        }
        if( root == "UV" )
        {
            return ImColor( 46, 116, 116 );
        }
        if( root == "Procedural" )
        {
            return ImColor( 132, 98, 36 );
        }
        if( root == "Color" )
        {
            return ImColor( 146, 64, 108 );
        }
        if( root == "Output" )
        {
            return ImColor( 40, 40, 44 );
        }
        return ImColor( 84, 84, 90 );
    }

    // A pin glyph: a filled circle when connected, a ring otherwise (squares for textures).
    void DrawPinIcon( ImDrawList* InDrawList, const ImVec2& InCenter, float InRadius, ValueType InType, bool InConnected )
    {
        const ImU32 color = TypeColor( InType );
        if( IsTextureType( InType ) )
        {
            const ImVec2 a( InCenter.x - InRadius, InCenter.y - InRadius );
            const ImVec2 b( InCenter.x + InRadius, InCenter.y + InRadius );
            if( InConnected )
            {
                InDrawList->AddRectFilled( a, b, color, 2.f );
            }
            else
            {
                InDrawList->AddRect( a, b, color, 2.f, 0, 1.5f );
            }
            return;
        }
        if( InConnected )
        {
            InDrawList->AddCircleFilled( InCenter, InRadius, color, 12 );
        }
        else
        {
            InDrawList->AddCircle( InCenter, InRadius, color, 12, 1.5f );
        }
    }

    // Case-insensitive fuzzy match: every character of InNeedle appears in order. Higher is better
    // (substring and word-start matches rank first).
    int MatchScore( const std::string& InText, const std::string& InNeedle )
    {
        if( InNeedle.empty() )
        {
            return 1;
        }
        auto lower = []( std::string InValue ) {
            std::transform( InValue.begin(), InValue.end(), InValue.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
            return InValue;
        };
        const std::string text = lower( InText );
        const std::string needle = lower( InNeedle );
        const size_t substring = text.find( needle );
        if( substring != std::string::npos )
        {
            return 1000 - static_cast<int>( substring ) * 4 + ( substring == 0 ? 200 : 0 );
        }
        size_t at = 0;
        int score = 100;
        for( const char c : needle )
        {
            const size_t found = text.find( c, at );
            if( found == std::string::npos )
            {
                return 0;
            }
            score -= static_cast<int>( found - at );
            at = found + 1;
        }
        return std::max( score, 1 );
    }

    const char* kTargetDisplayNames[] = { "Lit", "Unlit", "Particle", "Decal", "Post Process", "Custom Lit" };
}


ShaderGraphDocument::ShaderGraphDocument( const std::string& InPath )
    : m_path( InPath )
    , m_fullPath( Path( InPath ).FullPath )
{
    const size_t slash = InPath.find_last_of( "/\\" );
    m_title = slash == std::string::npos ? InPath : InPath.substr( slash + 1 );
    ed::Config config;
    // Node positions live in the graph (undoable, versioned), not in a settings file.
    config.SettingsFile = nullptr;
    m_editor = ed::CreateEditor( &config );
    m_preview = std::make_unique<ShaderGraphPreview>( InPath );
    m_lastEdit = std::chrono::steady_clock::now();
}


ShaderGraphDocument::~ShaderGraphDocument()
{
    if( m_editor )
    {
        ed::DestroyEditor( m_editor );
    }
}


bool ShaderGraphDocument::Load( std::string* OutError )
{
    std::string error;
    if( !m_graph.Load( m_fullPath, &error ) )
    {
        if( OutError )
        {
            *OutError = error;
        }
        return false;
    }
    m_undo.Clear();
    m_undo.MarkSaved();
    m_allPositionsDirty = true;
    m_frameOnNextDraw = true;
    m_needsCompile = true;
    return true;
}


bool ShaderGraphDocument::Save()
{
    if( !m_graph.Save( m_fullPath ) )
    {
        YIKES( "Couldn't save shader graph " + m_path );
        return false;
    }
    m_undo.MarkSaved();
    const CompileResult result = CompileAndExport( m_fullPath, m_graph );
    m_result = result;
    m_exportPending = false;
    ++m_compileCount;
    if( !result.Success )
    {
        BRUH( "Saved " + m_path + " with errors:\n" + result.FormatDiagnostics( m_graph ) );
    }
    else
    {
        CLog::Log( CLog::LogType::Info, "Saved shader graph " + m_path );
    }
    return true;
}


bool ShaderGraphDocument::IsDirty() const
{
    return m_undo.IsDirty();
}


void ShaderGraphDocument::RequestFocus()
{
    m_focusRequested = true;
}


uint64_t ShaderGraphDocument::MakePinId( uint32_t InNode, size_t InIndex, bool InIsOutput )
{
    return ( static_cast<uint64_t>( InNode ) << 20 ) | ( static_cast<uint64_t>( InIndex ) << 1 ) | ( InIsOutput ? 1u : 0u );
}


bool ShaderGraphDocument::DecodePin( uint64_t InPinId, uint32_t& OutNode, std::string& OutPin, bool& OutIsOutput ) const
{
    OutNode = static_cast<uint32_t>( InPinId >> 20 );
    OutIsOutput = ( InPinId & 1u ) != 0;
    const size_t index = static_cast<size_t>( ( InPinId >> 1 ) & 0x7FFFFu );
    const Node* node = m_graph.FindNode( OutNode );
    const NodeDef* def = node ? NodeRegistry::Get().Find( node->Type ) : nullptr;
    if( !def )
    {
        return false;
    }
    std::vector<PinDef> inputs;
    std::vector<PinDef> outputs;
    def->BuildPins( m_graph, *node, inputs, outputs );
    const std::vector<PinDef>& pins = OutIsOutput ? outputs : inputs;
    if( index >= pins.size() )
    {
        return false;
    }
    OutPin = pins[index].Name;
    return true;
}


ValueType ShaderGraphDocument::GetPinType( uint32_t InNode, const std::string& InPin, bool InIsOutput ) const
{
    auto resolved = m_result.PinTypes.find( { InNode, std::string( InIsOutput ? "out:" : "in:" ) + InPin } );
    if( resolved != m_result.PinTypes.end() )
    {
        return resolved->second;
    }
    const Node* node = m_graph.FindNode( InNode );
    const NodeDef* def = node ? NodeRegistry::Get().Find( node->Type ) : nullptr;
    if( def )
    {
        std::vector<PinDef> inputs;
        std::vector<PinDef> outputs;
        def->BuildPins( m_graph, *node, inputs, outputs );
        for( const PinDef& pin : InIsOutput ? outputs : inputs )
        {
            if( pin.Name == InPin )
            {
                return pin.Type;
            }
        }
    }
    return ValueType::Float;
}


bool ShaderGraphDocument::WouldCreateLoop( uint32_t InFromNode, uint32_t InToNode ) const
{
    // A loop exists if InToNode already feeds InFromNode.
    std::vector<uint32_t> stack = { InFromNode };
    std::set<uint32_t> seen;
    while( !stack.empty() )
    {
        const uint32_t current = stack.back();
        stack.pop_back();
        if( current == InToNode )
        {
            return true;
        }
        if( !seen.insert( current ).second )
        {
            continue;
        }
        for( const Link& link : m_graph.Links )
        {
            if( link.ToNode == current )
            {
                stack.push_back( link.FromNode );
            }
        }
    }
    return false;
}


bool ShaderGraphDocument::CanConnect( uint32_t InFromNode, const std::string& InFromPin, uint32_t InToNode, const std::string& InToPin, std::string* OutReason ) const
{
    if( InFromNode == InToNode )
    {
        if( OutReason )
        {
            *OutReason = "Can't connect a node to itself";
        }
        return false;
    }
    if( WouldCreateLoop( InFromNode, InToNode ) )
    {
        if( OutReason )
        {
            *OutReason = "That would make a loop";
        }
        return false;
    }
    const ValueType from = GetPinType( InFromNode, InFromPin, true );
    ValueType to = GetPinType( InToNode, InToPin, false );
    // Dynamic inputs accept any vector (they resize to fit).
    const Node* toNode = m_graph.FindNode( InToNode );
    const NodeDef* def = toNode ? NodeRegistry::Get().Find( toNode->Type ) : nullptr;
    if( def )
    {
        std::vector<PinDef> inputs;
        std::vector<PinDef> outputs;
        def->BuildPins( m_graph, *toNode, inputs, outputs );
        for( const PinDef& pin : inputs )
        {
            if( pin.Name == InToPin && pin.DynamicGroup >= 0 && IsVectorType( from ) )
            {
                to = from;
            }
        }
    }
    if( !CanConvert( from, to ) )
    {
        if( OutReason )
        {
            *OutReason = std::string( GetValueTypeDisplayName( from ) ) + " can't connect to " + GetValueTypeDisplayName( to );
        }
        return false;
    }
    return true;
}


std::vector<std::string> ShaderGraphDocument::GetNodeErrors( uint32_t InNode ) const
{
    std::vector<std::string> errors;
    for( const Diagnostic& diagnostic : m_preview->GetShaderErrors() )
    {
        if( diagnostic.NodeId == InNode )
        {
            errors.push_back( "Shader: " + diagnostic.Message );
        }
    }
    for( const Diagnostic& diagnostic : m_result.Diagnostics )
    {
        if( diagnostic.NodeId == InNode )
        {
            errors.push_back( std::string( diagnostic.Level == Diagnostic::Severity::Error ? "Error: " : "Warning: " ) + diagnostic.Message );
        }
    }
    return errors;
}


// --- Editing --------------------------------------------------------------------------------

void ShaderGraphDocument::BeginEdit( const std::string& InName, const std::string& InMergeKey )
{
    if( m_editDepth++ > 0 )
    {
        return;
    }
    m_editBefore = json();
    m_graph.Serialize( m_editBefore );
    m_editName = InName;
    m_editMergeKey = InMergeKey;
}


void ShaderGraphDocument::EndEdit()
{
    if( --m_editDepth > 0 )
    {
        return;
    }
    json after;
    m_graph.Serialize( after );
    if( after == m_editBefore )
    {
        return;
    }
    const ImGuiID interaction = m_editMergeKey.empty() ? 0 : ImGui::GetActiveID();
    m_undo.Push( std::make_unique<GraphSnapshotCommand>( *this, std::move( m_editBefore ), std::move( after ), m_editName, m_editMergeKey, interaction ) );
    m_needsCompile = true;
    m_lastEdit = std::chrono::steady_clock::now();
}


void ShaderGraphDocument::ApplySnapshot( const json& InSnapshot )
{
    m_graph.Deserialize( InSnapshot );
    m_allPositionsDirty = true;
    m_needsCompile = true;
    m_lastEdit = std::chrono::steady_clock::now();
    if( m_inspectedNode && !m_graph.FindNode( m_inspectedNode ) )
    {
        m_inspectedNode = 0;
    }
}


uint32_t ShaderGraphDocument::AddNode( const std::string& InType, const Vector2& InPosition, const json& InSettings )
{
    if( !NodeRegistry::Get().Find( InType ) )
    {
        return 0;
    }
    GraphEditScope edit( *this, "Add " + NodeRegistry::Get().Find( InType )->DisplayName );
    Node& node = m_graph.AddNode( InType, InPosition );
    if( InSettings.is_object() )
    {
        for( auto it = InSettings.begin(); it != InSettings.end(); ++it )
        {
            node.Settings[it.key()] = it.value();
        }
    }
    m_positionDirty.insert( node.Id );
    return node.Id;
}


void ShaderGraphDocument::RemoveNodes( const std::vector<uint32_t>& InIds )
{
    GraphEditScope edit( *this, InIds.size() == 1 ? "Delete Node" : "Delete Nodes" );
    for( const uint32_t id : InIds )
    {
        m_graph.RemoveNode( id );
        if( id == m_inspectedNode )
        {
            m_inspectedNode = 0;
        }
    }
    m_graph.Groups.erase( std::remove_if( m_graph.Groups.begin(), m_graph.Groups.end(), [&InIds]( const Group& InGroup ) {
        return std::find( InIds.begin(), InIds.end(), InGroup.Id ) != InIds.end();
    } ), m_graph.Groups.end() );
}


bool ShaderGraphDocument::Connect( uint32_t InFromNode, const std::string& InFromPin, uint32_t InToNode, const std::string& InToPin, std::string* OutError )
{
    if( !CanConnect( InFromNode, InFromPin, InToNode, InToPin, OutError ) )
    {
        return false;
    }
    GraphEditScope edit( *this, "Connect" );
    m_graph.Connect( InFromNode, InFromPin, InToNode, InToPin );
    return true;
}


void ShaderGraphDocument::Disconnect( uint32_t InToNode, const std::string& InToPin )
{
    GraphEditScope edit( *this, "Disconnect" );
    m_graph.Disconnect( InToNode, InToPin );
}


void ShaderGraphDocument::SetNodeSetting( uint32_t InNode, const std::string& InSetting, const json& InValue )
{
    Node* node = m_graph.FindNode( InNode );
    if( !node )
    {
        return;
    }
    GraphEditScope edit( *this, "Edit " + InSetting, "setting:" + std::to_string( InNode ) + ":" + InSetting );
    node = m_graph.FindNode( InNode );
    node->Settings[InSetting] = InValue;
}


void ShaderGraphDocument::SetInputDefault( uint32_t InNode, const std::string& InPin, const Vector4& InValue )
{
    if( !m_graph.FindNode( InNode ) )
    {
        return;
    }
    GraphEditScope edit( *this, "Edit " + InPin, "default:" + std::to_string( InNode ) + ":" + InPin );
    m_graph.FindNode( InNode )->SetDefault( InPin, InValue );
}


std::string ShaderGraphDocument::AddProperty( const std::string& InName, ValueType InType )
{
    GraphEditScope edit( *this, "Add Property" );
    std::string name = InName;
    for( int suffix = 2;; ++suffix )
    {
        const bool taken = std::any_of( m_graph.Properties.begin(), m_graph.Properties.end(), [&name]( const Property& InProperty ) { return InProperty.Name == name; } );
        if( !taken )
        {
            break;
        }
        name = InName + " " + std::to_string( suffix );
    }
    Property& property = m_graph.AddProperty( name, InType );
    return property.Id;
}


void ShaderGraphDocument::SetTarget( Target InTarget )
{
    GraphEditScope edit( *this, "Change Target" );
    m_graph.Settings.GraphTarget = InTarget;
    // Drop links into output pins the new target doesn't have.
    const Node* output = m_graph.FindOutputNode();
    if( !output )
    {
        return;
    }
    std::vector<PinDef> pins;
    if( const TargetDef* target = GetTargetDef( InTarget ) )
    {
        target->BuildOutputPins( m_graph.Settings, pins );
    }
    const uint32_t outputId = output->Id;
    m_graph.Links.erase( std::remove_if( m_graph.Links.begin(), m_graph.Links.end(), [&]( const Link& InLink ) {
        return InLink.ToNode == outputId && std::none_of( pins.begin(), pins.end(), [&]( const PinDef& InPin ) { return InPin.Name == InLink.ToPin; } );
    } ), m_graph.Links.end() );
}


void ShaderGraphDocument::CompileNow()
{
    CompileOptions options;
    options.GraphPath = m_path;
    m_result = Compile( m_graph, options );
    m_needsCompile = false;
    ++m_compileCount;
    // The preview program: recompiled in the background only when its code changed (editing a
    // constant just updates uniforms).
    CompileOptions previewOptions = options;
    previewOptions.Preview = true;
    m_preview->Update( Compile( m_graph, previewOptions ), m_graph );
    // Live: successful compiles go straight to the scene (hot reload picks the files up).
    m_exportPending = m_liveExport && m_result.Success;
}


// --- Actions ----------------------------------------------------------------------------------

bool ShaderGraphDocument::ClaimsAction( const std::string& InId ) const
{
    if( !m_isFocused )
    {
        return false;
    }
    static const char* kClaimed[] = { "File.Save", "Edit.Undo", "Edit.Redo", "Edit.Cut", "Edit.Copy", "Edit.Paste", "Edit.Duplicate", "Edit.Delete", "Edit.SelectAll", "Edit.Deselect" };
    return std::any_of( std::begin( kClaimed ), std::end( kClaimed ), [&InId]( const char* InClaimed ) { return InId == InClaimed; } );
}


void ShaderGraphDocument::ExecuteAction( const std::string& InId )
{
    ed::SetCurrentEditor( m_editor );
    if( InId == "File.Save" )
    {
        Save();
    }
    else if( InId == "Edit.Undo" )
    {
        m_undo.Undo();
    }
    else if( InId == "Edit.Redo" )
    {
        m_undo.Redo();
    }
    else if( InId == "Edit.Copy" )
    {
        CopySelection( false );
    }
    else if( InId == "Edit.Cut" )
    {
        CopySelection( true );
    }
    else if( InId == "Edit.Paste" )
    {
        const ImVec2 mouse = ed::ScreenToCanvas( ImGui::GetMousePos() );
        Paste( mouse );
    }
    else if( InId == "Edit.Duplicate" )
    {
        DuplicateSelection();
    }
    else if( InId == "Edit.Delete" )
    {
        DeleteSelection();
    }
    else if( InId == "Edit.SelectAll" )
    {
        for( const Node& node : m_graph.Nodes )
        {
            ed::SelectNode( static_cast<uint64_t>( node.Id ), true );
        }
    }
    else if( InId == "Edit.Deselect" )
    {
        ed::ClearSelection();
    }
    ed::SetCurrentEditor( nullptr );
}


std::vector<uint32_t> ShaderGraphDocument::GetSelectedNodes() const
{
    std::vector<uint32_t> result;
    const int count = ed::GetSelectedObjectCount();
    if( count <= 0 )
    {
        return result;
    }
    std::vector<ed::NodeId> nodes( static_cast<size_t>( count ) );
    const int found = ed::GetSelectedNodes( nodes.data(), count );
    for( int i = 0; i < found; ++i )
    {
        result.push_back( static_cast<uint32_t>( nodes[i].Get() ) );
    }
    return result;
}


void ShaderGraphDocument::CopySelection( bool InCut )
{
    const std::vector<uint32_t> selected = GetSelectedNodes();
    json clipboard;
    clipboard["ShaderGraphClipboard"] = 1;
    json& nodes = clipboard["Nodes"];
    nodes = json::array();
    std::set<std::string> properties;
    for( const uint32_t id : selected )
    {
        const Node* node = m_graph.FindNode( id );
        if( !node || node->Type == "Output" )
        {
            continue;
        }
        json entry;
        node->Serialize( entry );
        nodes.push_back( entry );
        if( node->Type == "Property" )
        {
            properties.insert( node->Settings.value( "Property", std::string() ) );
        }
    }
    if( nodes.empty() )
    {
        return;
    }
    json& links = clipboard["Links"];
    links = json::array();
    for( const Link& link : m_graph.Links )
    {
        const bool from = std::find( selected.begin(), selected.end(), link.FromNode ) != selected.end();
        const bool to = std::find( selected.begin(), selected.end(), link.ToNode ) != selected.end();
        if( from && to )
        {
            links.push_back( json::array( { link.FromNode, link.FromPin, link.ToNode, link.ToPin } ) );
        }
    }
    json& copiedProperties = clipboard["Properties"];
    copiedProperties = json::array();
    for( const std::string& id : properties )
    {
        if( const Property* property = m_graph.FindProperty( id ) )
        {
            json entry;
            property->Serialize( entry );
            copiedProperties.push_back( entry );
        }
    }
    ImGui::SetClipboardText( clipboard.dump().c_str() );
    if( InCut )
    {
        std::vector<uint32_t> removable;
        for( const uint32_t id : selected )
        {
            const Node* node = m_graph.FindNode( id );
            if( node && node->Type != "Output" )
            {
                removable.push_back( id );
            }
        }
        RemoveNodes( removable );
    }
}


void ShaderGraphDocument::Paste( const ImVec2& InCanvasPosition )
{
    const char* text = ImGui::GetClipboardText();
    if( !text )
    {
        return;
    }
    const json clipboard = json::parse( text, nullptr, false );
    if( clipboard.is_discarded() || !clipboard.contains( "ShaderGraphClipboard" ) )
    {
        return;
    }
    GraphEditScope edit( *this, "Paste" );
    // Properties from another graph come along (matched by id, then by name).
    std::map<std::string, std::string> propertyIds;
    for( const json& entry : clipboard.value( "Properties", json::array() ) )
    {
        Property property;
        property.Deserialize( entry );
        if( m_graph.FindProperty( property.Id ) )
        {
            propertyIds[property.Id] = property.Id;
            continue;
        }
        auto byName = std::find_if( m_graph.Properties.begin(), m_graph.Properties.end(), [&property]( const Property& InExisting ) { return InExisting.Name == property.Name; } );
        if( byName != m_graph.Properties.end() )
        {
            propertyIds[property.Id] = byName->Id;
            continue;
        }
        const std::string oldId = property.Id;
        property.Id = m_graph.AllocatePropertyId();
        m_graph.Properties.push_back( property );
        propertyIds[oldId] = property.Id;
    }

    // New ids, positions relative to the copied group's top left placed at the mouse.
    std::map<uint32_t, uint32_t> idMap;
    Vector2 minimum( 1e9f, 1e9f );
    for( const json& entry : clipboard.value( "Nodes", json::array() ) )
    {
        Node node;
        node.Deserialize( entry );
        minimum = Vector2( std::min( minimum.x, node.Position.x ), std::min( minimum.y, node.Position.y ) );
    }
    ed::ClearSelection();
    for( const json& entry : clipboard.value( "Nodes", json::array() ) )
    {
        Node node;
        node.Deserialize( entry );
        if( node.Type == "Output" || !NodeRegistry::Get().Find( node.Type ) )
        {
            continue;
        }
        const uint32_t oldId = node.Id;
        node.Id = m_graph.AllocateId();
        node.Position = Vector2( InCanvasPosition.x + node.Position.x - minimum.x, InCanvasPosition.y + node.Position.y - minimum.y );
        if( node.Type == "Property" )
        {
            const std::string id = node.Settings.value( "Property", std::string() );
            if( propertyIds.count( id ) )
            {
                node.Settings["Property"] = propertyIds[id];
            }
        }
        idMap[oldId] = node.Id;
        m_graph.Nodes.push_back( node );
        m_positionDirty.insert( node.Id );
        ed::SelectNode( static_cast<uint64_t>( node.Id ), true );
    }
    for( const json& entry : clipboard.value( "Links", json::array() ) )
    {
        if( entry.is_array() && entry.size() == 4 && idMap.count( entry[0].get<uint32_t>() ) && idMap.count( entry[2].get<uint32_t>() ) )
        {
            m_graph.Connect( idMap[entry[0].get<uint32_t>()], entry[1].get<std::string>(), idMap[entry[2].get<uint32_t>()], entry[3].get<std::string>() );
        }
    }
}


void ShaderGraphDocument::DuplicateSelection()
{
    const std::vector<uint32_t> selected = GetSelectedNodes();
    if( selected.empty() )
    {
        return;
    }
    const std::string previous = ImGui::GetClipboardText() ? ImGui::GetClipboardText() : "";
    CopySelection( false );
    Vector2 minimum( 1e9f, 1e9f );
    for( const uint32_t id : selected )
    {
        if( const Node* node = m_graph.FindNode( id ) )
        {
            minimum = Vector2( std::min( minimum.x, node->Position.x ), std::min( minimum.y, node->Position.y ) );
        }
    }
    Paste( ImVec2( minimum.x + 40.f, minimum.y + 40.f ) );
    ImGui::SetClipboardText( previous.c_str() );
}


void ShaderGraphDocument::DeleteSelection()
{
    std::vector<uint32_t> removable;
    for( const uint32_t id : GetSelectedNodes() )
    {
        const Node* node = m_graph.FindNode( id );
        if( ( node && node->Type != "Output" ) || std::any_of( m_graph.Groups.begin(), m_graph.Groups.end(), [id]( const Group& InGroup ) { return InGroup.Id == id; } ) )
        {
            removable.push_back( id );
        }
    }
    const int linkCount = ed::GetSelectedObjectCount();
    std::vector<ed::LinkId> links( static_cast<size_t>( std::max( linkCount, 1 ) ) );
    const int selectedLinks = ed::GetSelectedLinks( links.data(), linkCount );
    if( removable.empty() && selectedLinks <= 0 )
    {
        return;
    }
    GraphEditScope edit( *this, "Delete" );
    for( int i = 0; i < selectedLinks; ++i )
    {
        // Link ids are their input pin's id.
        uint32_t node = 0;
        std::string pin;
        bool isOutput = false;
        if( DecodePin( links[i].Get(), node, pin, isOutput ) && !isOutput )
        {
            m_graph.Disconnect( node, pin );
        }
    }
    if( !removable.empty() )
    {
        RemoveNodes( removable );
    }
    ed::ClearSelection();
}


void ShaderGraphDocument::FrameSelection( bool InAll )
{
    if( InAll || ed::GetSelectedObjectCount() == 0 )
    {
        ed::NavigateToContent( 0.2f );
    }
    else
    {
        ed::NavigateToSelection( false, 0.2f );
    }
}


// --- Drawing ----------------------------------------------------------------------------------

bool ShaderGraphDocument::Draw()
{
    // Debounced compile: edits arrive every frame while dragging a value.
    const auto now = std::chrono::steady_clock::now();
    if( m_needsCompile && now - m_lastEdit > std::chrono::milliseconds( 120 ) )
    {
        CompileNow();
    }
    // Live export waits a little longer (and for the mouse to settle), since every export
    // recompiles the scene's shaders.
    if( m_exportPending && now - m_lastEdit > std::chrono::milliseconds( 400 ) && !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
    {
        std::string error;
        if( !WriteGeneratedFiles( m_result, GetGeneratedDirectory( m_fullPath ), &error ) )
        {
            YIKES( error );
        }
        m_exportPending = false;
    }

    const std::string windowName = m_title + ( IsDirty() ? " *" : "" ) + "###ShaderGraph:" + m_path;
    if( m_focusRequested )
    {
        // Open next to the scene view the first time.
        if( ImGuiWindow* world = ImGui::FindWindowByName( "World View" ) )
        {
            if( world->DockId )
            {
                ImGui::SetNextWindowDockID( world->DockId, ImGuiCond_FirstUseEver );
            }
        }
        ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowSize( ImVec2( ImGuiTheme::Em( 71.f ), ImGuiTheme::Em( 42.f ) ), ImGuiCond_FirstUseEver );
        m_focusRequested = false;
    }
    // Half the usual padding: the canvas is the document and should use the space.
    const float padding = std::round( ImGuiTheme::PanelInset() * 0.5f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( padding, padding ) );
    bool open = true;
    const bool visible = ImGui::Begin( windowName.c_str(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ( IsDirty() ? ImGuiWindowFlags_UnsavedDocument : 0 ) );
    ImGui::PopStyleVar();
    m_isVisible = visible;
    m_isFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
    if( !open )
    {
        if( IsDirty() )
        {
            m_showClosePrompt = true;
        }
        else
        {
            m_isOpen = false;
        }
    }
    if( visible )
    {
        DrawToolbar();
        const size_t messageCount = m_result.Diagnostics.size() + m_preview->GetShaderErrors().size();
        const float messagesHeight = messageCount == 0 ? 0.f
            : std::min( ImGui::GetStyle().WindowPadding.y * 2.f + ImGui::GetTextLineHeightWithSpacing() * static_cast<float>( messageCount ), ImGuiTheme::Em( 7.75f ) );
        ImGui::BeginChild( "SidePanel", ImVec2( m_sidePanelWidth, 0.f ), true );
        DrawSidePanel( m_sidePanelWidth );
        ImGui::EndChild();
        ImGui::SameLine( 0.f, 0.f );
        // Splitter, as thick as the dock splitters.
        ImGui::InvisibleButton( "Splitter", ImVec2( ImGui::GetStyle().DockingSeparatorSize, ImGui::GetContentRegionAvail().y ) );
        if( ImGui::IsItemActive() )
        {
            m_sidePanelWidth = std::clamp( m_sidePanelWidth + ImGui::GetIO().MouseDelta.x, ImGuiTheme::Em( 10.f ), ImGuiTheme::Em( 39.f ) );
        }
        if( ImGui::IsItemHovered() || ImGui::IsItemActive() )
        {
            ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
        }
        ImGui::SameLine( 0.f, 0.f );
        ImGui::BeginGroup();
        ImGui::BeginChild( "Canvas", ImVec2( 0.f, -messagesHeight ), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
        DrawCanvas();
        ImGui::EndChild();
        if( messagesHeight > 0.f )
        {
            ImGui::BeginChild( "Messages", ImVec2( 0.f, 0.f ), true );
            DrawMessages();
            ImGui::EndChild();
        }
        ImGui::EndGroup();
    }
    ImGui::End();

    if( m_showCode )
    {
        DrawCodeWindow();
    }
    DrawClosePrompt();
    return m_isOpen;
}


void ShaderGraphDocument::DrawToolbar()
{
    if( ImGui::Button( "Save" ) )
    {
        Save();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled( !m_undo.CanUndo() );
    if( ImGui::Button( "Undo" ) )
    {
        m_undo.Undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled( !m_undo.CanRedo() );
    if( ImGui::Button( "Redo" ) )
    {
        m_undo.Redo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if( ImGui::Button( "Frame All" ) )
    {
        ed::SetCurrentEditor( m_editor );
        FrameSelection( true );
        ed::SetCurrentEditor( nullptr );
    }
    ImGui::SameLine();
    ImGui::Checkbox( "Live", &m_liveExport );
    if( ImGui::IsItemHovered() )
    {
        ImGui::SetTooltip( "Update the scene's materials as you edit (without saving the graph)." );
    }
    ImGui::SameLine();
    ImGui::Checkbox( "Show Code", &m_showCode );
    ImGui::SameLine();

    int errors = 0;
    int warnings = 0;
    for( const Diagnostic& diagnostic : m_result.Diagnostics )
    {
        ( diagnostic.Level == Diagnostic::Severity::Error ? errors : warnings )++;
    }
    ImGui::TextDisabled( "|" );
    ImGui::SameLine();
    ImGui::Text( "%s", kTargetDisplayNames[static_cast<int>( m_graph.Settings.GraphTarget )] );
    ImGui::SameLine();
    if( m_needsCompile )
    {
        ImGui::TextDisabled( "compiling..." );
    }
    else if( errors > 0 )
    {
        ImGui::TextColored( ImGuiTheme::ErrorText(), "%d error%s", errors, errors == 1 ? "" : "s" );
    }
    else
    {
        ImGui::TextColored( ImGuiTheme::SuccessText(), "compiled" );
    }
    if( warnings > 0 )
    {
        ImGui::SameLine();
        ImGui::TextColored( ImGuiTheme::WarningText(), "%d warning%s", warnings, warnings == 1 ? "" : "s" );
    }
    ImGui::SameLine();
    ImGui::TextDisabled( "|  Space: add node   F: frame   right-drag: pan" );
}


void ShaderGraphDocument::RenderPreviews( bgfx::ViewId InTileView, bgfx::ViewId InMainView )
{
    if( m_isVisible )
    {
        m_preview->Render( InTileView, InMainView );
    }
}


void ShaderGraphDocument::DrawMainPreview( float InWidth )
{
    const float size = std::min( ImGui::GetContentRegionAvail().x, ImGuiTheme::Em( 17.75f ) );
    ImVec2 uv0;
    ImVec2 uv1;
    if( m_preview->GetMain( uv0, uv1 ) )
    {
        ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( ImGui::GetContentRegionAvail().x - size ) * 0.5f );
        ImGui::Image( m_preview->GetMainTexture(), ImVec2( size, size ), uv0, uv1 );
    }
    else
    {
        ImGui::Dummy( ImVec2( size, size * 0.25f ) );
        ImGui::TextDisabled( m_preview->IsCompiling() ? "Compiling preview..." : "No preview yet." );
    }
    if( m_preview->IsCompiling() && m_preview->HasProgram() )
    {
        ImGui::TextDisabled( "updating..." );
    }
}


void ShaderGraphDocument::DrawSidePanel( float InWidth )
{
    if( !ImGui::BeginTabBar( "SidePanelTabs" ) )
    {
        return;
    }
    if( ImGui::BeginTabItem( "Node", nullptr, m_focusNodeInspector ? ImGuiTabItemFlags_SetSelected : 0 ) )
    {
        ImGui::BeginChild( "NodeInspector" );
        if( m_focusNodeInspector )
        {
            ImGui::SetScrollY( 0.f );
        }
        DrawNodeInspector();
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    m_focusNodeInspector = false;
    if( ImGui::BeginTabItem( "Blackboard" ) )
    {
        ImGui::BeginChild( "Blackboard" );
        DrawBlackboard();
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if( ImGui::BeginTabItem( "Graph" ) )
    {
        ImGui::BeginChild( "GraphSettings" );
        DrawGraphSettings();
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if( ImGui::BeginTabItem( "Preview" ) )
    {
        DrawMainPreview( InWidth );
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}


void ShaderGraphDocument::DrawGraphSettings()
{
    TargetSettings settings = m_graph.Settings;
    bool changed = false;

    auto combo = [&changed]( const char* InLabel, auto& InValue, const std::vector<const char*>& InNames ) {
        HavanaUtils::Label( InLabel );
        int index = static_cast<int>( InValue );
        if( ImGui::Combo( ( std::string( "##" ) + InLabel ).c_str(), &index, InNames.data(), static_cast<int>( InNames.size() ) ) )
        {
            InValue = static_cast<std::remove_reference_t<decltype( InValue )>>( index );
            changed = true;
        }
    };
    auto checkbox = [&changed]( const char* InLabel, bool& InValue, const char* InTooltip ) {
        HavanaUtils::Label( InLabel );
        if( ImGui::Checkbox( ( std::string( "##" ) + InLabel ).c_str(), &InValue ) )
        {
            changed = true;
        }
        if( ImGui::IsItemHovered() && InTooltip )
        {
            ImGui::SetTooltip( "%s", InTooltip );
        }
    };

    Target target = settings.GraphTarget;
    combo( "Target", target, { "Lit", "Unlit", "Particle", "Decal", "Post Process", "Custom Lit" } );
    if( target != settings.GraphTarget )
    {
        SetTarget( target );
        return;
    }
    if( const TargetDef* def = GetTargetDef( target ) )
    {
        ImGui::TextWrapped( "%s", def->Description.c_str() );
    }
    else
    {
        ImGui::TextColored( ImGuiTheme::WarningText(), "This target isn't available yet." );
    }

    const bool mesh = target == Target::Lit || target == Target::Unlit || target == Target::CustomLit;
    if( mesh )
    {
        combo( "Surface", settings.Surface, { "Opaque", "Transparent" } );
        if( settings.Surface == SurfaceType::Transparent )
        {
            combo( "Blend", settings.Blend, { "Alpha", "Premultiply", "Additive", "Multiply" } );
        }
        combo( "Render Face", settings.Faces, { "Front", "Back", "Both" } );
        combo( "Offset Space", settings.OffsetSpace, { "Object", "World" } );
        checkbox( "Alpha Clip", settings.AlphaClip, "Discard fragments with Alpha below Alpha Clip Threshold (also in shadows)." );
        checkbox( "Cast Shadows", settings.CastShadows, nullptr );
    }
    if( target == Target::Lit )
    {
        combo( "Normal Space", settings.FragmentNormalSpace, { "Tangent", "World" } );
    }
    if( target == Target::Unlit || target == Target::CustomLit )
    {
        checkbox( "Fog", settings.Fog, "Transparent surfaces apply height fog themselves." );
    }
    if( target == Target::PostProcess )
    {
        combo( "Injection", settings.Injection, { "Before Tonemap", "After Tonemap" } );
        if( ImGui::IsItemHovered() )
        {
            ImGui::SetTooltip( "Before: on HDR scene light (bloom and exposure see the result). After: on the display image." );
        }
        checkbox( "Scene Color Mips", settings.SceneColorMips, "Build a filtered scene-colour mip chain for texture2DLod(s_sgSceneColorMips, uv, lod) in Custom Functions. Adds a copy pass only for this graph." );
        if( settings.SceneColorMips )
        {
            const Property* threshold = m_graph.FindProperty( settings.SceneColorMipThreshold );
            if( ImGui::BeginCombo( "Mip Threshold", threshold ? threshold->Name.c_str() : "None" ) )
            {
                if( ImGui::Selectable( "None", settings.SceneColorMipThreshold.empty() ) )
                {
                    settings.SceneColorMipThreshold.clear();
                    changed = true;
                }
                for( const Property& property : m_graph.Properties )
                {
                    if( property.Type == ValueType::Float && ImGui::Selectable( property.Name.c_str(), property.Id == settings.SceneColorMipThreshold ) )
                    {
                        settings.SceneColorMipThreshold = property.Id;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip( "Optional Float property: soft-threshold bright light before filtering. Ordinary Scene Color reads stay unchanged." );
        }
    }

    if( changed )
    {
        GraphEditScope edit( *this, "Graph Settings" );
        m_graph.Settings = settings;
    }
}


void ShaderGraphDocument::DrawBlackboard()
{
    if( ImGui::Button( "+ Property" ) )
    {
        ImGui::OpenPopup( "AddProperty" );
    }
    if( ImGui::BeginPopup( "AddProperty" ) )
    {
        struct Choice
        {
            const char* Label;
            ValueType Type;
            bool Color;
        };
        static const Choice kChoices[] = {
            { "Float", ValueType::Float, false }, { "Vector 2", ValueType::Vec2, false }, { "Vector 3", ValueType::Vec3, false },
            { "Vector 4", ValueType::Vec4, false }, { "Color", ValueType::Vec4, true }, { "Boolean", ValueType::Bool, false },
            { "Texture 2D", ValueType::Texture2D, false }, { "Cubemap", ValueType::TextureCube, false }
        };
        for( const Choice& choice : kChoices )
        {
            if( ImGui::MenuItem( choice.Label ) )
            {
                const std::string id = AddProperty( choice.Label, choice.Type );
                if( choice.Color )
                {
                    GraphEditScope edit( *this, "Add Property" );
                    Property* property = m_graph.FindProperty( id );
                    property->IsColor = true;
                    property->Default = Vector4( 1.f, 1.f, 1.f, 1.f );
                }
                m_inspectedProperty = id;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled( "drag onto the canvas" );

    for( size_t i = 0; i < m_graph.Properties.size(); ++i )
    {
        Property& property = m_graph.Properties[i];
        ImGui::PushID( property.Id.c_str() );
        const bool selected = m_inspectedProperty == property.Id;
        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( TypeColor( property.Type ) ) );
        ImGui::Bullet();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        // The type name ends the row; the name takes the rest.
        const char* typeName = property.IsColor ? "Color" : GetValueTypeDisplayName( property.Type );
        const float typeWidth = ImGui::CalcTextSize( typeName ).x + ImGui::GetStyle().ItemSpacing.x;
        if( m_renamingProperty == property.Id )
        {
            ImGui::SetNextItemWidth( -typeWidth );
            ImGui::SetKeyboardFocusHere();
            if( ImGui::InputText( "##Rename", m_renameBuffer, sizeof( m_renameBuffer ), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll ) || ImGui::IsItemDeactivated() )
            {
                if( m_renameBuffer[0] != '\0' && property.Name != m_renameBuffer )
                {
                    GraphEditScope edit( *this, "Rename Property" );
                    m_graph.FindProperty( m_renamingProperty )->Name = m_renameBuffer;
                }
                m_renamingProperty.clear();
            }
        }
        else
        {
            if( ImGui::Selectable( property.Name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2( ImGui::GetContentRegionAvail().x - typeWidth, 0.f ) ) )
            {
                m_inspectedProperty = property.Id;
                m_inspectedNode = 0;
                if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                {
                    m_renamingProperty = property.Id;
                    std::snprintf( m_renameBuffer, sizeof( m_renameBuffer ), "%s", property.Name.c_str() );
                }
            }
            if( ImGui::BeginDragDropSource() )
            {
                ImGui::SetDragDropPayload( "SG_PROPERTY", property.Id.c_str(), property.Id.size() + 1 );
                ImGui::Text( "%s", property.Name.c_str() );
                ImGui::EndDragDropSource();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", typeName );
        ImGui::PopID();
    }

    // The selected property's details.
    Property* property = m_inspectedProperty.empty() ? nullptr : m_graph.FindProperty( m_inspectedProperty );
    if( !property )
    {
        return;
    }
    ImGui::PushID( "PropertyDetails" );
    ImGui::SeparatorText( property->Name.c_str() );
    Property edited = *property;
    bool changed = false;
    bool interactive = false;
    if( IsTextureType( edited.Type ) )
    {
        const float textureWidth = HavanaUtils::Label( "Texture" );
        if( ImGui::Button( ( edited.Texture.empty() ? std::string( "(none)" ) : edited.Texture ).c_str(), ImVec2( textureWidth, 0.f ) ) )
        {
            const std::string id = edited.Id;
            RequestAssetSelectionEvent evt( [this, id]( Path InSelected ) {
                GraphEditScope edit( *this, "Set Texture" );
                if( Property* target = m_graph.FindProperty( id ) )
                {
                    target->Texture = InSelected.GetLocalPathString();
                }
            }, AssetType::Texture );
            evt.Fire();
        }
        if( !edited.Texture.empty() )
        {
            HavanaUtils::SkipLabel();
            if( ImGui::Button( "Clear Texture" ) )
            {
                edited.Texture.clear();
                changed = true;
            }
        }
        int fallback = edited.DefaultTexture == "black" ? 1 : edited.DefaultTexture == "gray" ? 2 : edited.DefaultTexture == "normal" ? 3 : 0;
        const char* fallbacks[] = { "white", "black", "gray", "normal" };
        HavanaUtils::Label( "Fallback" );
        if( ImGui::Combo( "##Fallback", &fallback, fallbacks, 4 ) )
        {
            edited.DefaultTexture = fallbacks[fallback];
            changed = true;
        }
    }
    else if( edited.Type == ValueType::Bool )
    {
        bool value = edited.Default.x != 0.f;
        HavanaUtils::Label( "Default" );
        if( ImGui::Checkbox( "##Default", &value ) )
        {
            edited.Default.x = value ? 1.f : 0.f;
            changed = true;
        }
    }
    else
    {
        HavanaUtils::Label( "Default" );
        const int components = GetComponentCount( edited.Type );
        if( edited.IsColor && components >= 3 )
        {
            const ImGuiColorEditFlags flags = edited.IsHDR ? ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float : 0;
            changed = components == 4 ? ImGui::ColorEdit4( "##Default", &edited.Default.x, flags ) : ImGui::ColorEdit3( "##Default", &edited.Default.x, flags );
        }
        else if( components == 1 && edited.Min != edited.Max )
        {
            changed = ImGui::SliderFloat( "##Default", &edited.Default.x, edited.Min, edited.Max );
        }
        else
        {
            changed = ImGui::DragScalarN( "##Default", ImGuiDataType_Float, &edited.Default.x, components, 0.01f );
        }
        interactive = changed;
        if( components == 1 )
        {
            bool slider = edited.Min != edited.Max;
            HavanaUtils::Label( "Slider" );
            if( ImGui::Checkbox( "##Slider", &slider ) )
            {
                edited.Min = 0.f;
                edited.Max = slider ? 1.f : 0.f;
                changed = true;
            }
            if( slider )
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth( HavanaUtils::RowRight() - ImGui::GetCursorScreenPos().x );
                float range[2] = { edited.Min, edited.Max };
                if( ImGui::DragFloat2( "##Range", range, 0.01f ) )
                {
                    edited.Min = range[0];
                    edited.Max = range[1];
                    changed = true;
                    interactive = true;
                }
            }
        }
        if( components >= 3 )
        {
            HavanaUtils::Label( "Color" );
            changed |= ImGui::Checkbox( "##IsColor", &edited.IsColor );
            if( edited.IsColor )
            {
                ImGui::SameLine();
                changed |= ImGui::Checkbox( "HDR", &edited.IsHDR );
            }
        }
    }
    HavanaUtils::Label( "Exposed" );
    changed |= ImGui::Checkbox( "##Exposed", &edited.Exposed );
    if( ImGui::IsItemHovered() )
    {
        ImGui::SetTooltip( "Shown in the material inspector and settable per material." );
    }
    HavanaUtils::SkipLabel();
    if( ImGui::Button( "Delete Property" ) )
    {
        GraphEditScope edit( *this, "Delete Property" );
        m_graph.RemoveProperty( edited.Id );
        m_inspectedProperty.clear();
        ImGui::PopID();
        return;
    }
    ImGui::SameLine();
    auto position = std::find_if( m_graph.Properties.begin(), m_graph.Properties.end(), [&edited]( const Property& InProperty ) { return InProperty.Id == edited.Id; } );
    const size_t index = static_cast<size_t>( position - m_graph.Properties.begin() );
    ImGui::BeginDisabled( index == 0 );
    if( ImGui::ArrowButton( "##Up", ImGuiDir_Up ) )
    {
        GraphEditScope edit( *this, "Reorder Properties" );
        std::swap( m_graph.Properties[index], m_graph.Properties[index - 1] );
        ImGui::EndDisabled();
        ImGui::PopID();
        return;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled( index + 1 >= m_graph.Properties.size() );
    if( ImGui::ArrowButton( "##Down", ImGuiDir_Down ) )
    {
        GraphEditScope edit( *this, "Reorder Properties" );
        std::swap( m_graph.Properties[index], m_graph.Properties[index + 1] );
        ImGui::EndDisabled();
        ImGui::PopID();
        return;
    }
    ImGui::EndDisabled();
    if( changed )
    {
        GraphEditScope edit( *this, "Edit Property", interactive ? "property:" + edited.Id : "" );
        if( Property* target = m_graph.FindProperty( edited.Id ) )
        {
            *target = edited;
        }
    }
    ImGui::PopID();
}


void ShaderGraphDocument::DrawNodeInspector()
{
    Node* node = m_inspectedNode ? m_graph.FindNode( m_inspectedNode ) : nullptr;
    const NodeDef* def = node ? NodeRegistry::Get().Find( node->Type ) : nullptr;
    if( !node || !def )
    {
        ImGui::TextDisabled( "Select a node to edit it." );
        return;
    }
    const uint32_t id = node->Id;
    ImGui::PushID( static_cast<int>( id ) );
    ImGui::Text( "%s", def->GetTitle( m_graph, *node ).c_str() );
    ImGui::SameLine();
    ImGui::TextDisabled( "#%u", id );
    ImGui::TextDisabled( "%s", node->Type.c_str() );
    if( !def->Description.empty() )
    {
        ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
        ImGui::TextWrapped( "%s", def->Description.c_str() );
        ImGui::PopStyleColor();
    }
    for( const std::string& error : GetNodeErrors( id ) )
    {
        ImGui::PushStyleColor( ImGuiCol_Text, error.rfind( "Error", 0 ) == 0 ? ImGuiTheme::ErrorText() : ImGuiTheme::WarningText() );
        ImGui::TextWrapped( "%s", error.c_str() );
        ImGui::PopStyleColor();
    }
    if( !def->Settings.empty() )
    {
        ImGui::SeparatorText( "Settings" );
    }
    for( size_t i = 0; i < def->Settings.size(); ++i )
    {
        DrawSettingEditor( *m_graph.FindNode( id ), *def, i, false );
    }

    // Unconnected inputs, with full-size editors.
    std::vector<PinDef> inputs;
    std::vector<PinDef> outputs;
    def->BuildPins( m_graph, *m_graph.FindNode( id ), inputs, outputs );
    bool header = false;
    for( const PinDef& pin : inputs )
    {
        if( m_graph.FindInputLink( id, pin.Name ) || pin.DefaultBuiltin != Builtin::None || IsTextureType( pin.Type ) )
        {
            continue;
        }
        if( !header )
        {
            ImGui::SeparatorText( "Inputs" );
            header = true;
        }
        ImGui::PushID( pin.Name.c_str() );
        HavanaUtils::Label( pin.Name );
        DrawInlineValue( *m_graph.FindNode( id ), pin, GetPinType( id, pin.Name, false ), -1.f );
        if( !pin.Tooltip.empty() && ImGui::IsItemHovered() )
        {
            ImGui::SetTooltip( "%s", pin.Tooltip.c_str() );
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}


bool ShaderGraphDocument::DrawInlineValue( Node& InNode, const PinDef& InPin, ValueType InType, float InWidth )
{
    const Vector4* stored = InNode.FindDefault( InPin.Name );
    Vector4 value = stored ? *stored : InPin.Default;
    const uint32_t id = InNode.Id;
    bool changed = false;
    if( InType == ValueType::Bool )
    {
        bool enabled = value.x != 0.f;
        if( ImGui::Checkbox( "##Value", &enabled ) )
        {
            value.x = enabled ? 1.f : 0.f;
            changed = true;
        }
    }
    else if( IsVectorType( InType ) )
    {
        const int components = GetComponentCount( InType );
        if( InPin.IsColor && components >= 3 && InWidth < 0.f )
        {
            ImGui::SetNextItemWidth( -1.f );
            changed = ImGui::ColorEdit3( "##Value", &value.x, ImGuiColorEditFlags_Float );
        }
        else
        {
            ImGui::SetNextItemWidth( InWidth < 0.f ? -1.f : InWidth * static_cast<float>( components ) );
            changed = ImGui::DragScalarN( "##Value", ImGuiDataType_Float, &value.x, components, 0.01f, nullptr, nullptr, "%.3g" );
        }
    }
    if( changed )
    {
        // A dynamic input edited at one component keeps that value on every component, so it
        // still means the same thing once a wider vector widens the node.
        if( InPin.DynamicGroup >= 0 && GetComponentCount( InType ) == 1 )
        {
            value = Vector4( value.x );
        }
        SetInputDefault( id, InPin.Name, value );
    }
    return changed;
}


bool ShaderGraphDocument::DrawSettingEditor( Node& InNode, const NodeDef& InDef, size_t InSettingIndex, bool InCompact )
{
    const SettingDef& setting = InDef.Settings[InSettingIndex];
    const uint32_t id = InNode.Id;
    const json current = InNode.Settings.contains( setting.Name ) ? InNode.Settings[setting.Name] : setting.Default;
    json value = current;
    bool changed = false;
    ImGui::PushID( setting.Name.c_str() );
    if( !InCompact )
    {
        // Code and pin lists take the full width below their name; the rest are property rows.
        const bool block = setting.Kind == SettingKind::Code || setting.Kind == SettingKind::PinList;
        if( block )
        {
            ImGui::TextUnformatted( setting.Name.c_str() );
        }
        else
        {
            HavanaUtils::Label( setting.Name );
        }
        if( !setting.Tooltip.empty() && ImGui::IsItemHovered() )
        {
            ImGui::SetTooltip( "%s", setting.Tooltip.c_str() );
        }
        if( block )
        {
            ImGui::SetNextItemWidth( -1.f );
        }
    }
    switch( setting.Kind )
    {
    case SettingKind::Bool:
    {
        bool enabled = current.is_boolean() && current.get<bool>();
        if( ImGui::Checkbox( "##Setting", &enabled ) )
        {
            value = enabled;
            changed = true;
        }
        break;
    }
    case SettingKind::Int:
    {
        int number = current.is_number() ? current.get<int>() : 0;
        if( ImGui::InputInt( "##Setting", &number ) )
        {
            value = number;
            changed = true;
        }
        break;
    }
    case SettingKind::Float:
    {
        float number = current.is_number() ? current.get<float>() : 0.f;
        if( ImGui::DragFloat( "##Setting", &number, 0.01f ) )
        {
            value = number;
            changed = true;
        }
        break;
    }
    case SettingKind::Enum:
    {
        const std::string selected = current.is_string() ? current.get<std::string>() : std::string();
        if( ImGui::BeginCombo( "##Setting", selected.c_str() ) )
        {
            for( const std::string& option : setting.Options )
            {
                if( ImGui::Selectable( option.c_str(), option == selected ) )
                {
                    value = option;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        break;
    }
    case SettingKind::String:
    {
        std::string text = current.is_string() ? current.get<std::string>() : std::string();
        if( ImGui::InputText( "##Setting", &text ) )
        {
            value = text;
            changed = true;
        }
        break;
    }
    case SettingKind::Code:
    {
        std::string text = current.is_string() ? current.get<std::string>() : std::string();
        if( ImGui::InputTextMultiline( "##Setting", &text, ImVec2( -1.f, ImGui::GetTextLineHeight() * 10.f ), ImGuiInputTextFlags_AllowTabInput ) )
        {
            value = text;
            changed = true;
        }
        ImGui::TextDisabled( "Assign every output. bgfx shader dialect (vec3, mul, texture2D...)." );
        break;
    }
    case SettingKind::Texture:
    {
        std::string path = current.is_string() ? current.get<std::string>() : std::string();
        ImGui::SetNextItemWidth( std::max( 1.f, ImGui::GetContentRegionAvail().x - ImGuiTheme::IconButtonsWidth( 2 ) ) );
        if( ImGui::InputTextWithHint( "##Setting", "(none)", &path ) )
        {
            value = path;
            changed = true;
        }
        if( ImGui::IsItemHovered() && !path.empty() )
        {
            ImGui::SetTooltip( "%s", path.c_str() );
        }
        ImGui::SameLine( 0.f, ImGuiTheme::InnerGap() );
        if( ImGui::Button( "..", ImGuiTheme::IconButtonSize() ) )
        {
            const std::string name = setting.Name;
            RequestAssetSelectionEvent evt( [this, id, name]( Path InSelected ) {
                SetNodeSetting( id, name, InSelected.GetLocalPathString() );
            }, AssetType::Texture );
            evt.Fire();
        }
        ImGui::SameLine( 0.f, ImGuiTheme::InnerGap() );
        if( ImGui::Button( "x", ImGuiTheme::IconButtonSize() ) )
        {
            value = "";
            changed = true;
        }
        break;
    }
    case SettingKind::Property:
    {
        const std::string selected = current.is_string() ? current.get<std::string>() : std::string();
        const Property* property = m_graph.FindProperty( selected );
        if( ImGui::BeginCombo( "##Setting", property ? property->Name.c_str() : "(missing)" ) )
        {
            for( const Property& candidate : m_graph.Properties )
            {
                if( ImGui::Selectable( candidate.Name.c_str(), candidate.Id == selected ) )
                {
                    value = candidate.Id;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        break;
    }
    case SettingKind::Vector:
    case SettingKind::Color:
    {
        float components[4] = { 0.f, 0.f, 0.f, 0.f };
        if( current.is_array() )
        {
            for( size_t i = 0; i < current.size() && i < 4; ++i )
            {
                components[i] = current[i].is_number() ? current[i].get<float>() : 0.f;
            }
        }
        else if( current.is_number() )
        {
            components[0] = current.get<float>();
        }
        std::vector<PinDef> inputs;
        std::vector<PinDef> outputs;
        InDef.BuildPins( m_graph, InNode, inputs, outputs );
        const int count = outputs.empty() ? 4 : std::max( 1, GetComponentCount( outputs[0].Type ) );
        if( setting.Kind == SettingKind::Color )
        {
            const bool hdr = InNode.Settings.value( "HDR", false );
            const ImGuiColorEditFlags flags = ImGuiColorEditFlags_AlphaBar | ( hdr ? ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float : 0 );
            changed = InCompact ? ImGui::ColorEdit4( "##Setting", components, flags | ImGuiColorEditFlags_NoInputs ) : ImGui::ColorEdit4( "##Setting", components, flags );
        }
        else
        {
            changed = ImGui::DragScalarN( "##Setting", ImGuiDataType_Float, components, count, 0.01f, nullptr, nullptr, "%.3g" );
        }
        if( changed )
        {
            value = json::array( { components[0], components[1], components[2], components[3] } );
        }
        break;
    }
    case SettingKind::PinList:
    {
        json list = current.is_array() ? current : json::array();
        static const char* kTypes[] = { "Float", "Vec2", "Vec3", "Vec4", "Bool", "Texture2D", "TextureCube" };
        int remove = -1;
        for( size_t i = 0; i < list.size(); ++i )
        {
            ImGui::PushID( static_cast<int>( i ) );
            std::string name = list[i].value( "Name", std::string() );
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x * 0.5f );
            if( ImGui::InputText( "##Name", &name ) )
            {
                list[i]["Name"] = name;
                changed = true;
            }
            ImGui::SameLine();
            const std::string type = list[i].value( "Type", std::string( "Float" ) );
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - ImGuiTheme::IconButtonsWidth( 1 ) );
            if( ImGui::BeginCombo( "##Type", type.c_str() ) )
            {
                for( const char* option : kTypes )
                {
                    if( ImGui::Selectable( option, type == option ) )
                    {
                        list[i]["Type"] = option;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine( 0.f, ImGuiTheme::InnerGap() );
            if( ImGui::Button( "x", ImGuiTheme::IconButtonSize() ) )
            {
                remove = static_cast<int>( i );
            }
            ImGui::PopID();
        }
        if( remove >= 0 )
        {
            list.erase( static_cast<size_t>( remove ) );
            changed = true;
        }
        if( ImGui::Button( "+ Add" ) )
        {
            list.push_back( { { "Name", "Value" + std::to_string( list.size() ) }, { "Type", "Float" } } );
            changed = true;
        }
        if( changed )
        {
            value = list;
        }
        break;
    }
    }
    ImGui::PopID();
    if( changed )
    {
        SetNodeSetting( id, setting.Name, value );
    }
    return changed;
}


void ShaderGraphDocument::DrawCanvas()
{
    ed::SetCurrentEditor( m_editor );
    ed::Begin( "ShaderGraphCanvas" );
    // Our own actions (Delete, Copy, F...) handle the shortcuts.
    ed::EnableShortcuts( false );

    // Apply positions from the graph (new nodes, undo, load).
    for( const Node& node : m_graph.Nodes )
    {
        if( m_allPositionsDirty || m_positionDirty.count( node.Id ) )
        {
            ed::SetNodePosition( static_cast<uint64_t>( node.Id ), ImVec2( node.Position.x, node.Position.y ) );
        }
    }
    for( const Group& group : m_graph.Groups )
    {
        if( m_allPositionsDirty || m_positionDirty.count( group.Id ) )
        {
            ed::SetNodePosition( static_cast<uint64_t>( group.Id ), ImVec2( group.Position.x, group.Position.y ) );
            ed::SetGroupSize( static_cast<uint64_t>( group.Id ), ImVec2( group.Size.x, group.Size.y ) );
        }
    }
    m_allPositionsDirty = false;
    m_positionDirty.clear();

    DrawGroups();
    std::vector<uint32_t> ids;
    for( const Node& node : m_graph.Nodes )
    {
        ids.push_back( node.Id );
    }
    for( const uint32_t id : ids )
    {
        if( Node* node = m_graph.FindNode( id ) )
        {
            DrawNode( *node );
        }
    }

    // Links: the id of a link is its input pin's id (an input has at most one link).
    for( const Link& link : m_graph.Links )
    {
        const Node* from = m_graph.FindNode( link.FromNode );
        const Node* to = m_graph.FindNode( link.ToNode );
        const NodeDef* fromDef = from ? NodeRegistry::Get().Find( from->Type ) : nullptr;
        const NodeDef* toDef = to ? NodeRegistry::Get().Find( to->Type ) : nullptr;
        if( !fromDef || !toDef )
        {
            continue;
        }
        std::vector<PinDef> fromIn, fromOut, toIn, toOut;
        fromDef->BuildPins( m_graph, *from, fromIn, fromOut );
        toDef->BuildPins( m_graph, *to, toIn, toOut );
        auto outIt = std::find_if( fromOut.begin(), fromOut.end(), [&link]( const PinDef& InPin ) { return InPin.Name == link.FromPin; } );
        auto inIt = std::find_if( toIn.begin(), toIn.end(), [&link]( const PinDef& InPin ) { return InPin.Name == link.ToPin; } );
        if( outIt == fromOut.end() || inIt == toIn.end() )
        {
            continue;
        }
        const uint64_t start = MakePinId( link.FromNode, static_cast<size_t>( outIt - fromOut.begin() ), true );
        const uint64_t end = MakePinId( link.ToNode, static_cast<size_t>( inIt - toIn.begin() ), false );
        const ImColor color = TypeColor( GetPinType( link.FromNode, link.FromPin, true ) );
        ed::Link( end, start, end, color, 2.f );
    }

    HandleCreateAndDelete();

    // Keyboard: Space opens the palette, F frames.
    const bool canvasHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows );
    if( m_isFocused && canvasHovered && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() )
    {
        if( ImGui::IsKeyPressed( ImGuiKey_Space, false ) )
        {
            OpenPalette( ed::ScreenToCanvas( ImGui::GetMousePos() ), 0 );
        }
        if( ImGui::IsKeyPressed( ImGuiKey_F, false ) )
        {
            FrameSelection( false );
        }
    }

    ed::Suspend();
    ed::NodeId contextNode;
    ed::LinkId contextLink;
    if( ed::ShowNodeContextMenu( &contextNode ) )
    {
        m_contextNode = static_cast<uint32_t>( contextNode.Get() );
        if( !ed::IsNodeSelected( contextNode ) )
        {
            ed::SelectNode( contextNode );
        }
        ImGui::OpenPopup( "SGNodeMenu" );
    }
    else if( ed::ShowLinkContextMenu( &contextLink ) )
    {
        m_contextLink = contextLink.Get();
        ImGui::OpenPopup( "SGLinkMenu" );
    }
    else if( ed::ShowBackgroundContextMenu() )
    {
        OpenPalette( ed::ScreenToCanvas( ImGui::GetMousePos() ), 0 );
    }
    DrawContextMenus();
    DrawPalette();

    // Tooltips for the hovered pin / node problems.
    if( !ImGui::IsPopupOpen( "", ImGuiPopupFlags_AnyPopupId ) )
    {
        const ed::PinId hoveredPin = ed::GetHoveredPin();
        const ed::NodeId hoveredNode = ed::GetHoveredNode();
        uint32_t node = 0;
        std::string pin;
        bool isOutput = false;
        if( hoveredPin && DecodePin( hoveredPin.Get(), node, pin, isOutput ) )
        {
            ImGui::SetTooltip( "%s (%s)", pin.c_str(), GetValueTypeDisplayName( GetPinType( node, pin, isOutput ) ) );
        }
        else if( hoveredNode )
        {
            const std::vector<std::string> errors = GetNodeErrors( static_cast<uint32_t>( hoveredNode.Get() ) );
            if( !errors.empty() )
            {
                ImGui::BeginTooltip();
                for( const std::string& error : errors )
                {
                    ImGui::TextUnformatted( error.c_str() );
                }
                ImGui::EndTooltip();
            }
        }
    }
    ed::Resume();

    if( m_frameOnNextDraw && ed::GetNodeCount() > 0 )
    {
        // Node sizes are only known once they've been drawn: frame a couple of frames in.
        ed::NavigateToContent( 0.f );
        m_frameOnNextDraw = false;
    }
    // End processes clicks and drags. Read the resulting selection and positions afterwards;
    // Begin snapshots selection next frame, so checking before End loses mouse selections.
    // The node library uses a one-pixel drag threshold. Suppress its drag detection until
    // ImGui's normal screen-pixel threshold is reached, then restore the input for other UI.
    ImGuiIO& io = ImGui::GetIO();
    const float dragDistance = io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left];
    if( dragDistance < io.MouseDragThreshold * io.MouseDragThreshold )
    {
        io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] = 0.f;
    }
    ed::End();
    io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] = dragDistance;
    if( ed::HasSelectionChanged() )
    {
        const std::vector<uint32_t> selected = GetSelectedNodes();
        if( selected.size() == 1 && m_graph.FindNode( selected[0] ) )
        {
            m_inspectedNode = selected[0];
            m_inspectedProperty.clear();
            m_focusNodeInspector = true;
        }
        else
        {
            m_inspectedNode = 0;
        }
    }

    SyncNodePositions();

    // Properties dragged from the blackboard.
    if( ImGui::BeginDragDropTargetCustom( ImGui::GetCurrentWindow()->InnerRect, ImGui::GetID( "CanvasDrop" ) ) )
    {
        if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( "SG_PROPERTY" ) )
        {
            const std::string propertyId( static_cast<const char*>( payload->Data ) );
            const ImVec2 position = ed::ScreenToCanvas( ImGui::GetMousePos() );
            AddNode( "Property", Vector2( position.x, position.y ), json{ { "Property", propertyId } } );
        }
        ImGui::EndDragDropTarget();
    }
    ed::SetCurrentEditor( nullptr );
}


void ShaderGraphDocument::DrawGroups()
{
    for( const Group& group : m_graph.Groups )
    {
        ed::PushStyleColor( ed::StyleColor_NodeBg, ImVec4( group.Color.x, group.Color.y, group.Color.z, group.Color.w ) );
        ed::PushStyleColor( ed::StyleColor_NodeBorder, ImVec4( group.Color.x, group.Color.y, group.Color.z, 0.8f ) );
        ed::BeginNode( static_cast<uint64_t>( group.Id ) );
        ImGui::PushID( static_cast<int>( group.Id ) );
        ImGui::TextUnformatted( group.Title.c_str() );
        ed::Group( ImVec2( group.Size.x, group.Size.y ) );
        ImGui::PopID();
        ed::EndNode();
        ed::PopStyleColor( 2 );
    }
}


void ShaderGraphDocument::DrawNode( Node& InNode )
{
    const NodeDef* def = NodeRegistry::Get().Find( InNode.Type );
    const uint32_t id = InNode.Id;
    std::vector<PinDef> inputs;
    std::vector<PinDef> outputs;
    if( def )
    {
        def->BuildPins( m_graph, InNode, inputs, outputs );
    }
    bool hasError = false;
    bool hasWarning = false;
    for( const Diagnostic& diagnostic : m_result.Diagnostics )
    {
        if( diagnostic.NodeId == id )
        {
            ( diagnostic.Level == Diagnostic::Severity::Error ? hasError : hasWarning ) = true;
        }
    }
    for( const Diagnostic& diagnostic : m_preview->GetShaderErrors() )
    {
        hasError = hasError || diagnostic.NodeId == id;
    }

    ed::PushStyleVar( ed::StyleVar_NodePadding, ImVec4( 8.f, 6.f, 8.f, 8.f ) );
    if( hasError )
    {
        ed::PushStyleColor( ed::StyleColor_NodeBorder, ImVec4( 1.f, 0.3f, 0.25f, 1.f ) );
    }
    ed::BeginNode( static_cast<uint64_t>( id ) );
    ImGui::PushID( static_cast<int>( id ) );

    const std::string title = def ? def->GetTitle( m_graph, InNode ) : InNode.Type + " (unknown)";
    ImGui::TextUnformatted( title.c_str() );
    if( hasWarning && !hasError )
    {
        ImGui::SameLine();
        ImGui::TextColored( ImGuiTheme::WarningText(), "!" );
    }
    const float headerBottom = ImGui::GetItemRectMax().y + 6.f;
    ImGui::Dummy( ImVec2( 0.f, 4.f ) );

    if( def && !def->Settings.empty() )
    {
        if( ImGui::SmallButton( "Settings..." ) )
        {
            ed::SelectNode( static_cast<uint64_t>( id ) );
            m_inspectedNode = id;
            m_inspectedProperty.clear();
            m_focusNodeInspector = true;
        }
        if( const SettingDef* texture = def->FindSetting( "Texture" ); texture && texture->Kind == SettingKind::Texture )
        {
            const std::string path = InNode.Settings.value( "Texture", std::string() );
            const std::string filename = path.substr( path.find_last_of( "/\\" ) + 1 );
            ImGui::TextDisabled( "%s", path.empty() ? "(no texture)" : filename.c_str() );
            if( ImGui::IsItemHovered() && !path.empty() )
            {
                ImGui::SetTooltip( "%s", path.c_str() );
            }
        }
    }

    // Enum settings on the node itself (they open a popup outside the canvas).
    if( def )
    {
        for( const SettingDef& setting : def->Settings )
        {
            if( setting.Kind != SettingKind::Enum )
            {
                continue;
            }
            const std::string value = InNode.Settings.value( setting.Name, setting.Default.is_string() ? setting.Default.get<std::string>() : std::string() );
            if( ImGui::SmallButton( ( value + " v##" + setting.Name ).c_str() ) )
            {
                m_enumPopupNode = id;
                m_enumPopupSetting = setting.Name;
                m_enumPopupRequested = true;
            }
        }
    }

    // Inputs on the left.
    ImGui::BeginGroup();
    for( size_t i = 0; i < inputs.size(); ++i )
    {
        const PinDef& pin = inputs[i];
        const bool linked = m_graph.FindInputLink( id, pin.Name ) != nullptr;
        if( pin.Hidden && !linked )
        {
            continue;
        }
        const ValueType type = GetPinType( id, pin.Name, false );
        ImGui::PushID( pin.Name.c_str() );
        ed::BeginPin( MakePinId( id, i, false ), ed::PinKind::Input );
        ed::PinPivotAlignment( ImVec2( 0.f, 0.5f ) );
        ed::PinPivotSize( ImVec2( 0.f, 0.f ) );
        const ImVec2 icon = ImGui::GetCursorScreenPos();
        ImGui::Dummy( ImVec2( 14.f, ImGui::GetTextLineHeight() ) );
        DrawPinIcon( ImGui::GetWindowDrawList(), ImVec2( icon.x + 6.f, icon.y + ImGui::GetTextLineHeight() * 0.5f ), 5.f, type, linked );
        ed::EndPin();
        ImGui::SameLine();
        ImGui::TextUnformatted( pin.Name.c_str() );
        if( !linked )
        {
            if( pin.DefaultBuiltin != Builtin::None )
            {
                ImGui::SameLine();
                ImGui::TextDisabled( "(%s)", GetBuiltinName( pin.DefaultBuiltin ) );
            }
            else if( IsVectorType( type ) || type == ValueType::Bool )
            {
                ImGui::SameLine();
                DrawInlineValue( InNode, pin, type, 42.f );
            }
        }
        ImGui::PopID();
    }
    ImGui::EndGroup();

    // Outputs on the right, right-aligned.
    ImGui::SameLine( 0.f, 18.f );
    ImGui::BeginGroup();
    float widest = 0.f;
    for( const PinDef& pin : outputs )
    {
        widest = std::max( widest, ImGui::CalcTextSize( pin.Name.c_str() ).x );
    }
    const float column = ImGui::GetCursorPosX();
    for( size_t i = 0; i < outputs.size(); ++i )
    {
        const PinDef& pin = outputs[i];
        const ValueType type = GetPinType( id, pin.Name, true );
        const bool linked = std::any_of( m_graph.Links.begin(), m_graph.Links.end(), [&]( const Link& InLink ) { return InLink.FromNode == id && InLink.FromPin == pin.Name; } );
        ImGui::SetCursorPosX( column + widest - ImGui::CalcTextSize( pin.Name.c_str() ).x );
        ImGui::TextUnformatted( pin.Name.c_str() );
        ImGui::SameLine();
        ed::BeginPin( MakePinId( id, i, true ), ed::PinKind::Output );
        ed::PinPivotAlignment( ImVec2( 1.f, 0.5f ) );
        ed::PinPivotSize( ImVec2( 0.f, 0.f ) );
        const ImVec2 icon = ImGui::GetCursorScreenPos();
        ImGui::Dummy( ImVec2( 14.f, ImGui::GetTextLineHeight() ) );
        DrawPinIcon( ImGui::GetWindowDrawList(), ImVec2( icon.x + 8.f, icon.y + ImGui::GetTextLineHeight() * 0.5f ), 5.f, type, linked );
        ed::EndPin();
    }
    ImGui::EndGroup();

    ImVec2 uv0;
    ImVec2 uv1;
    if( def && def->HasPreview && InNode.PreviewExpanded && m_preview->GetTile( id, uv0, uv1 ) )
    {
        ImGui::Image( m_preview->GetAtlas(), ImVec2( 112.f, 112.f ), uv0, uv1 );
    }

    ImGui::PopID();
    ed::EndNode();
    if( hasError )
    {
        ed::PopStyleColor();
    }
    ed::PopStyleVar();

    // Header band behind the title, coloured by category.
    const ImVec2 nodeMin = ImGui::GetItemRectMin();
    const ImVec2 nodeMax = ImGui::GetItemRectMax();
    if( ImDrawList* drawList = ed::GetNodeBackgroundDrawList( static_cast<uint64_t>( id ) ) )
    {
        const float rounding = ed::GetStyle().NodeRounding;
        ImColor header = hasError ? ImColor( 150, 40, 36 ) : CategoryColor( def ? def->Category : "" );
        header.Value.w = 0.9f;
        drawList->AddRectFilled( ImVec2( nodeMin.x + 1.f, nodeMin.y + 1.f ), ImVec2( nodeMax.x - 1.f, headerBottom ), header, rounding, ImDrawFlags_RoundCornersTop );
    }
}


void ShaderGraphDocument::HandleCreateAndDelete()
{
    if( ed::BeginCreate( ImColor( 255, 255, 255 ), 2.f ) )
    {
        ed::PinId a;
        ed::PinId b;
        if( ed::QueryNewLink( &a, &b ) && a && b )
        {
            uint32_t nodeA = 0;
            uint32_t nodeB = 0;
            std::string pinA;
            std::string pinB;
            bool outA = false;
            bool outB = false;
            if( DecodePin( a.Get(), nodeA, pinA, outA ) && DecodePin( b.Get(), nodeB, pinB, outB ) )
            {
                std::string reason;
                bool valid = outA != outB;
                if( !valid )
                {
                    reason = "Connect an output to an input";
                }
                const uint32_t fromNode = outA ? nodeA : nodeB;
                const std::string fromPin = outA ? pinA : pinB;
                const uint32_t toNode = outA ? nodeB : nodeA;
                const std::string toPin = outA ? pinB : pinA;
                valid = valid && CanConnect( fromNode, fromPin, toNode, toPin, &reason );
                if( !valid )
                {
                    ed::RejectNewItem( ImColor( 255, 80, 70 ), 2.f );
                    ed::Suspend();
                    ImGui::SetTooltip( "%s", reason.c_str() );
                    ed::Resume();
                }
                else if( ed::AcceptNewItem( TypeColor( GetPinType( fromNode, fromPin, true ) ), 2.5f ) )
                {
                    Connect( fromNode, fromPin, toNode, toPin );
                }
            }
        }
        ed::PinId pin;
        if( ed::QueryNewNode( &pin ) && pin )
        {
            ed::Suspend();
            ImGui::SetTooltip( "Release to add a node" );
            ed::Resume();
            if( ed::AcceptNewItem() )
            {
                OpenPalette( ed::ScreenToCanvas( ImGui::GetMousePos() ), pin.Get() );
            }
        }
        ed::EndCreate();
    }

    if( ed::BeginDelete() )
    {
        ed::LinkId link;
        while( ed::QueryDeletedLink( &link ) )
        {
            uint32_t node = 0;
            std::string pin;
            bool isOutput = false;
            if( DecodePin( link.Get(), node, pin, isOutput ) && !isOutput && ed::AcceptDeletedItem() )
            {
                Disconnect( node, pin );
            }
        }
        std::vector<uint32_t> removed;
        ed::NodeId nodeId;
        while( ed::QueryDeletedNode( &nodeId ) )
        {
            const Node* node = m_graph.FindNode( static_cast<uint32_t>( nodeId.Get() ) );
            if( node && node->Type == "Output" )
            {
                ed::RejectDeletedItem();
            }
            else if( ed::AcceptDeletedItem() )
            {
                removed.push_back( static_cast<uint32_t>( nodeId.Get() ) );
            }
        }
        if( !removed.empty() )
        {
            RemoveNodes( removed );
        }
        ed::EndDelete();
    }
}


void ShaderGraphDocument::SyncNodePositions()
{
    // Commit moves once the mouse is released (a drag is one undo step).
    if( ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
    {
        return;
    }
    bool moved = false;
    for( const Node& node : m_graph.Nodes )
    {
        const ImVec2 position = ed::GetNodePosition( static_cast<uint64_t>( node.Id ) );
        if( position.x != FLT_MAX && ( std::abs( position.x - node.Position.x ) > 0.5f || std::abs( position.y - node.Position.y ) > 0.5f ) )
        {
            moved = true;
            break;
        }
    }
    for( const Group& group : m_graph.Groups )
    {
        const ImVec2 position = ed::GetNodePosition( static_cast<uint64_t>( group.Id ) );
        if( position.x != FLT_MAX && ( std::abs( position.x - group.Position.x ) > 0.5f || std::abs( position.y - group.Position.y ) > 0.5f ) )
        {
            moved = true;
            break;
        }
    }
    if( !moved )
    {
        return;
    }
    GraphEditScope edit( *this, "Move" );
    for( Node& node : m_graph.Nodes )
    {
        const ImVec2 position = ed::GetNodePosition( static_cast<uint64_t>( node.Id ) );
        if( position.x != FLT_MAX )
        {
            node.Position = Vector2( std::round( position.x ), std::round( position.y ) );
        }
    }
    for( Group& group : m_graph.Groups )
    {
        const ImVec2 position = ed::GetNodePosition( static_cast<uint64_t>( group.Id ) );
        if( position.x != FLT_MAX )
        {
            group.Position = Vector2( std::round( position.x ), std::round( position.y ) );
        }
    }
    // Rounded positions go back to the canvas so the next comparison is exact.
    m_allPositionsDirty = true;
}


void ShaderGraphDocument::OpenPalette( const ImVec2& InCanvasPosition, uint64_t InFromPin )
{
    m_paletteRequested = true;
    m_paletteCanvasPosition = InCanvasPosition;
    m_paletteFromPin = InFromPin;
    m_paletteFilter[0] = '\0';
    m_paletteSelection = 0;
}


void ShaderGraphDocument::DrawPalette()
{
    if( m_paletteRequested )
    {
        ImGui::OpenPopup( "SGPalette" );
        m_paletteRequested = false;
    }
    if( m_enumPopupRequested )
    {
        ImGui::OpenPopup( "SGEnumPopup" );
        m_enumPopupRequested = false;
    }
    if( ImGui::BeginPopup( "SGEnumPopup" ) )
    {
        Node* node = m_graph.FindNode( m_enumPopupNode );
        const NodeDef* def = node ? NodeRegistry::Get().Find( node->Type ) : nullptr;
        const SettingDef* setting = def ? def->FindSetting( m_enumPopupSetting ) : nullptr;
        if( setting )
        {
            const std::string current = node->Settings.value( setting->Name, setting->Default.is_string() ? setting->Default.get<std::string>() : std::string() );
            for( const std::string& option : setting->Options )
            {
                if( ImGui::Selectable( option.c_str(), option == current ) )
                {
                    SetNodeSetting( m_enumPopupNode, setting->Name, option );
                }
            }
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSizeConstraints( ImVec2( ImGuiTheme::Em( 15.5f ), ImGuiTheme::Em( 6.75f ) ), ImVec2( ImGuiTheme::Em( 20.f ), ImGuiTheme::Em( 25.5f ) ) );
    if( !ImGui::BeginPopup( "SGPalette" ) )
    {
        return;
    }
    // What the dragged pin can connect to.
    uint32_t fromNode = 0;
    std::string fromPin;
    bool fromOutput = false;
    const bool fromPinValid = m_paletteFromPin && DecodePin( m_paletteFromPin, fromNode, fromPin, fromOutput );
    const ValueType fromType = fromPinValid ? GetPinType( fromNode, fromPin, fromOutput ) : ValueType::Float;

    struct Entry
    {
        const NodeDef* Def = nullptr;
        std::string PropertyId;
        std::string Label;
        std::string Category;
        int Score = 0;
        std::string ConnectPin;
    };
    std::vector<Entry> entries;
    const std::string filter = m_paletteFilter;
    auto compatiblePin = [&]( const NodeDef& InDef, const json& InSettings ) -> std::string {
        if( !fromPinValid )
        {
            return "";
        }
        Node probe;
        probe.Type = InDef.Type;
        probe.Settings = InSettings;
        std::vector<PinDef> inputs;
        std::vector<PinDef> outputs;
        InDef.BuildPins( m_graph, probe, inputs, outputs );
        for( const PinDef& pin : fromOutput ? inputs : outputs )
        {
            const bool fits = fromOutput ? ( CanConvert( fromType, pin.Type ) || ( pin.DynamicGroup >= 0 && IsVectorType( fromType ) ) )
                                         : ( CanConvert( pin.Type, fromType ) || ( pin.DynamicGroup >= 0 && IsVectorType( fromType ) ) );
            if( fits )
            {
                return pin.Name;
            }
        }
        return "\x01";
    };
    for( const NodeDef* def : NodeRegistry::Get().GetAll() )
    {
        if( def->Hidden || !def->SupportsTarget( m_graph.Settings.GraphTarget ) )
        {
            continue;
        }
        Entry entry;
        entry.Def = def;
        entry.Label = def->DisplayName;
        entry.Category = def->Category;
        entry.Score = std::max( MatchScore( def->DisplayName, filter ) * 2, std::max( MatchScore( def->Keywords, filter ), MatchScore( def->Category, filter ) / 2 ) );
        entry.ConnectPin = compatiblePin( *def, json::object() );
        if( entry.Score > 0 && entry.ConnectPin != "\x01" )
        {
            entries.push_back( entry );
        }
    }
    if( const NodeDef* propertyDef = NodeRegistry::Get().Find( "Property" ) )
    {
        for( const Property& property : m_graph.Properties )
        {
            Entry entry;
            entry.Def = propertyDef;
            entry.PropertyId = property.Id;
            entry.Label = property.Name;
            entry.Category = "Properties";
            entry.Score = MatchScore( property.Name, filter ) * 2 + 1;
            entry.ConnectPin = compatiblePin( *propertyDef, json{ { "Property", property.Id } } );
            if( entry.Score > 1 && entry.ConnectPin != "\x01" )
            {
                entries.push_back( entry );
            }
        }
    }
    if( !filter.empty() )
    {
        std::stable_sort( entries.begin(), entries.end(), []( const Entry& InA, const Entry& InB ) { return InA.Score > InB.Score; } );
    }
    else
    {
        std::stable_sort( entries.begin(), entries.end(), []( const Entry& InA, const Entry& InB ) { return InA.Category < InB.Category; } );
    }

    if( ImGui::IsWindowAppearing() )
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth( -1.f );
    if( ImGui::InputTextWithHint( "##Filter", fromPinValid ? "Search (compatible nodes)" : "Search nodes", m_paletteFilter, sizeof( m_paletteFilter ) ) )
    {
        m_paletteSelection = 0;
    }
    ImGui::Separator();
    if( ImGui::IsKeyPressed( ImGuiKey_DownArrow ) )
    {
        m_paletteSelection = std::min( m_paletteSelection + 1, static_cast<int>( entries.size() ) - 1 );
    }
    if( ImGui::IsKeyPressed( ImGuiKey_UpArrow ) )
    {
        m_paletteSelection = std::max( m_paletteSelection - 1, 0 );
    }
    const Entry* chosen = nullptr;
    if( ImGui::IsKeyPressed( ImGuiKey_Enter ) && m_paletteSelection < static_cast<int>( entries.size() ) )
    {
        chosen = &entries[m_paletteSelection];
    }

    ImGui::BeginChild( "Entries", ImVec2( 0.f, ImGuiTheme::Em( 20.f ) ) );
    std::string lastCategory;
    for( size_t i = 0; i < entries.size(); ++i )
    {
        const Entry& entry = entries[i];
        if( filter.empty() && entry.Category != lastCategory )
        {
            ImGui::SeparatorText( entry.Category.c_str() );
            lastCategory = entry.Category;
        }
        const bool selected = static_cast<int>( i ) == m_paletteSelection;
        if( ImGui::Selectable( ( entry.Label + "##entry" + std::to_string( i ) ).c_str(), selected ) )
        {
            chosen = &entry;
        }
        if( selected && ImGui::IsKeyPressed( ImGuiKey_DownArrow, true ) )
        {
            ImGui::SetScrollHereY();
        }
        if( !filter.empty() )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", entry.Category.c_str() );
        }
        if( ImGui::IsItemHovered() && !entry.Def->Description.empty() )
        {
            ImGui::SetTooltip( "%s", entry.Def->Description.c_str() );
        }
    }
    ImGui::EndChild();

    if( chosen )
    {
        // Copy what we need: AddNode changes the graph the entries were built from.
        const std::string type = chosen->Def->Type;
        const std::string propertyId = chosen->PropertyId;
        const std::string connectPin = chosen->ConnectPin;
        BeginEdit( "Add Node" );
        const json settings = propertyId.empty() ? json::object() : json{ { "Property", propertyId } };
        const uint32_t created = AddNode( type, Vector2( m_paletteCanvasPosition.x, m_paletteCanvasPosition.y ), settings );
        if( created && fromPinValid && !connectPin.empty() )
        {
            if( fromOutput )
            {
                Connect( fromNode, fromPin, created, connectPin );
            }
            else
            {
                Connect( created, connectPin, fromNode, fromPin );
            }
        }
        EndEdit();
        m_inspectedNode = created;
        ImGui::CloseCurrentPopup();
    }
    if( ImGui::IsKeyPressed( ImGuiKey_Escape ) )
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}


void ShaderGraphDocument::DrawContextMenus()
{
    if( ImGui::BeginPopup( "SGNodeMenu" ) )
    {
        Node* node = m_graph.FindNode( m_contextNode );
        const bool isOutput = node && node->Type == "Output";
        if( ImGui::MenuItem( "Copy", nullptr, false, !isOutput ) )
        {
            CopySelection( false );
        }
        if( ImGui::MenuItem( "Duplicate", nullptr, false, !isOutput ) )
        {
            DuplicateSelection();
        }
        static const std::map<std::string, std::pair<ValueType, bool>> kConvertible = {
            { "Float", { ValueType::Float, false } }, { "Vector2", { ValueType::Vec2, false } }, { "Vector3", { ValueType::Vec3, false } },
            { "Vector4", { ValueType::Vec4, false } }, { "Color", { ValueType::Vec4, true } }
        };
        auto convertible = node ? kConvertible.find( node->Type ) : kConvertible.end();
        if( convertible != kConvertible.end() && ImGui::MenuItem( "Convert to Property" ) )
        {
            GraphEditScope edit( *this, "Convert to Property" );
            const std::string id = AddProperty( node->Type == "Color" ? "Color" : node->Type, convertible->second.first );
            node = m_graph.FindNode( m_contextNode );
            Property* property = m_graph.FindProperty( id );
            const json value = node->Settings.value( "Value", json::array( { 0.f, 0.f, 0.f, 0.f } ) );
            if( value.is_array() )
            {
                float* components[4] = { &property->Default.x, &property->Default.y, &property->Default.z, &property->Default.w };
                for( size_t i = 0; i < value.size() && i < 4; ++i )
                {
                    *components[i] = value[i].get<float>();
                }
            }
            else if( value.is_number() )
            {
                property->Default.x = value.get<float>();
            }
            property->IsColor = convertible->second.second;
            node->Type = "Property";
            node->Settings = json{ { "Property", id } };
            m_inspectedProperty = id;
        }
        if( node && !node->PreviewExpanded && ImGui::MenuItem( "Show Preview" ) )
        {
            GraphEditScope edit( *this, "Show Preview" );
            m_graph.FindNode( m_contextNode )->PreviewExpanded = true;
        }
        else if( node && node->PreviewExpanded && ImGui::MenuItem( "Hide Preview" ) )
        {
            GraphEditScope edit( *this, "Hide Preview" );
            m_graph.FindNode( m_contextNode )->PreviewExpanded = false;
        }
        ImGui::Separator();
        if( ImGui::MenuItem( "Group Selection" ) )
        {
            const std::vector<uint32_t> selected = GetSelectedNodes();
            ImVec2 minimum( FLT_MAX, FLT_MAX );
            ImVec2 maximum( -FLT_MAX, -FLT_MAX );
            for( const uint32_t id : selected )
            {
                const ImVec2 position = ed::GetNodePosition( static_cast<uint64_t>( id ) );
                const ImVec2 size = ed::GetNodeSize( static_cast<uint64_t>( id ) );
                minimum = ImVec2( std::min( minimum.x, position.x ), std::min( minimum.y, position.y ) );
                maximum = ImVec2( std::max( maximum.x, position.x + size.x ), std::max( maximum.y, position.y + size.y ) );
            }
            if( minimum.x < maximum.x )
            {
                GraphEditScope edit( *this, "Group" );
                Group group;
                group.Id = m_graph.AllocateId();
                group.Position = Vector2( minimum.x - 20.f, minimum.y - 50.f );
                group.Size = Vector2( maximum.x - minimum.x + 40.f, maximum.y - minimum.y + 60.f );
                m_graph.Groups.push_back( group );
                m_positionDirty.insert( group.Id );
            }
        }
        if( ImGui::MenuItem( "Delete", "Del", false, !isOutput ) )
        {
            DeleteSelection();
        }
        ImGui::EndPopup();
    }
    if( ImGui::BeginPopup( "SGLinkMenu" ) )
    {
        uint32_t node = 0;
        std::string pin;
        bool isOutput = false;
        const bool valid = DecodePin( m_contextLink, node, pin, isOutput ) && !isOutput;
        if( valid && ImGui::MenuItem( "Insert Reroute" ) )
        {
            const Link* link = m_graph.FindInputLink( node, pin );
            if( link )
            {
                const Link copy = *link;
                const Node* from = m_graph.FindNode( copy.FromNode );
                const Node* to = m_graph.FindNode( copy.ToNode );
                const Vector2 middle( ( from->Position.x + to->Position.x ) * 0.5f, ( from->Position.y + to->Position.y ) * 0.5f );
                GraphEditScope edit( *this, "Insert Reroute" );
                const uint32_t reroute = AddNode( "Reroute", middle );
                m_graph.Connect( copy.FromNode, copy.FromPin, reroute, "In" );
                m_graph.Connect( reroute, "Out", copy.ToNode, copy.ToPin );
            }
        }
        if( valid && ImGui::MenuItem( "Delete Link" ) )
        {
            Disconnect( node, pin );
        }
        ImGui::EndPopup();
    }
}


void ShaderGraphDocument::DrawMessages()
{
    std::vector<Diagnostic> all = m_result.Diagnostics;
    for( Diagnostic diagnostic : m_preview->GetShaderErrors() )
    {
        diagnostic.Message = "shader: " + diagnostic.Message;
        all.push_back( diagnostic );
    }
    for( size_t i = 0; i < all.size(); ++i )
    {
        const Diagnostic& diagnostic = all[i];
        const bool error = diagnostic.Level == Diagnostic::Severity::Error;
        ImGui::PushStyleColor( ImGuiCol_Text, error ? ImGuiTheme::ErrorText() : ImGuiTheme::WarningText() );
        std::string label = error ? "error  " : "warning  ";
        const Node* node = diagnostic.NodeId ? m_graph.FindNode( diagnostic.NodeId ) : nullptr;
        if( node )
        {
            const NodeDef* def = NodeRegistry::Get().Find( node->Type );
            label += ( def ? def->GetTitle( m_graph, *node ) : node->Type ) + ": ";
        }
        label += diagnostic.Message + "##message" + std::to_string( i );
        if( ImGui::Selectable( label.c_str() ) && node )
        {
            ed::SetCurrentEditor( m_editor );
            ed::SelectNode( static_cast<uint64_t>( node->Id ) );
            ed::NavigateToSelection( false, 0.25f );
            ed::SetCurrentEditor( nullptr );
            m_inspectedNode = node->Id;
        }
        ImGui::PopStyleColor();
    }
}


void ShaderGraphDocument::DrawCodeWindow()
{
    ImGui::SetNextWindowSize( ImVec2( ImGuiTheme::Em( 42.f ), ImGuiTheme::Em( 35.5f ) ), ImGuiCond_FirstUseEver );
    if( ImGui::Begin( ( m_title + " - Generated Code###SGCode:" + m_path ).c_str(), &m_showCode ) )
    {
        if( ImGui::BeginTabBar( "Files" ) )
        {
            for( const GeneratedFile& file : m_result.Files )
            {
                if( ImGui::BeginTabItem( file.Name.c_str() ) )
                {
                    std::string text = file.Source;
                    ImGui::InputTextMultiline( "##Code", &text, ImVec2( -1.f, -1.f ), ImGuiInputTextFlags_ReadOnly );
                    ImGui::EndTabItem();
                }
            }
            if( ImGui::BeginTabItem( "Material.json" ) )
            {
                std::string text = m_result.Manifest.dump( 1, '\t' );
                ImGui::InputTextMultiline( "##Code", &text, ImVec2( -1.f, -1.f ), ImGuiInputTextFlags_ReadOnly );
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}


void ShaderGraphDocument::DrawClosePrompt()
{
    const std::string popup = "Unsaved Shader Graph###SGClose:" + m_path;
    if( m_showClosePrompt )
    {
        ImGui::OpenPopup( popup.c_str() );
        m_showClosePrompt = false;
    }
    if( ImGui::BeginPopupModal( popup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        ImGui::Text( "Save changes to %s?", m_title.c_str() );
        if( ImGui::Button( "Save" ) )
        {
            Save();
            m_isOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if( ImGui::Button( "Don't Save" ) )
        {
            // The scene may be showing unsaved live edits: put the saved graph's shaders back.
            Load();
            CompileAndExport( m_fullPath, m_graph );
            m_isOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if( ImGui::Button( "Cancel" ) )
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
#endif
