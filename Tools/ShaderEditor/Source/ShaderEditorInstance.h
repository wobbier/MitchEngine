#pragma once
#include <string>
#include "Editor\imgui_node_editor_internal.h"
#include "utilities\builders.h"
#include "utilities\widgets.h"
#include "Core\Node.h"
#include "Graphics\Texture.h"

class BasicShaderMasterNode;

static inline ImRect ImGui_GetItemRect()
{
    return ImRect( ImGui::GetItemRectMin(), ImGui::GetItemRectMax() );
}

static inline ImRect ImRect_Expanded( const ImRect& rect, float x, float y )
{
    auto result = rect;
    result.Min.x -= x;
    result.Min.y -= y;
    result.Max.x += x;
    result.Max.y += y;
    return result;
}

static bool Splitter( bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size = -1.0f )
{
    using namespace ImGui;
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiID id = window->GetID( "##Splitter" );
    ImRect bb;
    bb.Min = window->DC.CursorPos + ( split_vertically ? ImVec2( *size1, 0.0f ) : ImVec2( 0.0f, *size1 ) );
    bb.Max = bb.Min + CalcItemSize( split_vertically ? ImVec2( thickness, splitter_long_axis_size ) : ImVec2( splitter_long_axis_size, thickness ), 0.0f, 0.0f );
    return SplitterBehavior( bb, id, split_vertically ? ImGuiAxis_X : ImGuiAxis_Y, size1, size2, min_size1, min_size2, 0.0f );
}

namespace ed = ax::NodeEditor;
namespace util = ax::NodeEditor::Utilities;

using namespace ax;

using ax::Widgets::IconType;

class ShaderEditorInstance
{
    friend class ShaderEditor;
public:
    ShaderEditorInstance();

    void Init( const Path& inShaderFilepath );
    void Start();

    void OnUpdate();
private:
    ed::EditorContext* m_editorContext = nullptr;
    std::string m_shaderFileName;
    std::string m_configPath;
    Path m_shaderFilePath;

    // Save States
    void SaveGraph( Path& inPath );
    void LoadGraph( Path& inPath );

    Node* SpawnNodeFromString( int& inNodeId, std::string& id, json* inJson = nullptr );

    // Copied

    void BlueprintStart();
    void DrawBlueprint();

    void HandleLinks();

    void DrawOrdinals();

    void DrawComments();
    void DrawBasicNodes();


    ed::Config config;
    struct LinkInfo
    {
        ed::LinkId Id;
        ed::PinId  InputId;
        ed::PinId  OutputId;
    };

    bool                 m_FirstFrame = true;    // Flag set for first frame only, some action need to be executed once.
    ImVector<LinkInfo>   m_LinksOG;                // List of live links. It is dynamic unless you want to create read-only view over nodes.
    int                  m_NextLinkId = 1;     // Counter to help generate link ids. In real application this will probably based on pointer to user data structure.


    int                  m_NextId = 1;
    const int            m_PinIconSize = 24;
    std::vector<Node*>   m_Nodes;
    std::vector<Link>    m_Links;
    ImTextureID          m_HeaderBackground = nullptr;
    ImTextureID          m_SaveIcon = nullptr;
    ImTextureID          m_RestoreIcon = nullptr;
    const float          m_TouchTime = 1.0f;
    std::map<ed::NodeId, float, NodeIdLess> m_NodeTouchTime;
    bool                 m_ShowOrdinals = false;

    std::vector<Node*>   m_exported;

    int GetNextId()
    {
        return m_NextId++;
    }

    //ed::NodeId GetNextNodeId()
    //{
    //    return ed::NodeId(GetNextId());
    //}

    ed::LinkId GetNextLinkId()
    {
        return ed::LinkId( m_NextLinkId++ );
    }

    void TouchNode( ed::NodeId id )
    {
        m_NodeTouchTime[id] = m_TouchTime;
    }

    float GetTouchProgress( ed::NodeId id )
    {
        auto it = m_NodeTouchTime.find( id );
        if( it != m_NodeTouchTime.end() && it->second > 0.0f )
            return ( m_TouchTime - it->second ) / m_TouchTime;
        else
            return 0.0f;
    }

    void UpdateTouch()
    {
        const auto deltaTime = ImGui::GetIO().DeltaTime;
        for( auto& entry : m_NodeTouchTime )
        {
            if( entry.second > 0.0f )
                entry.second -= deltaTime;
        }
    }

    Node* FindNode( ed::NodeId id )
    {
        for( auto node : m_Nodes )
            if( node->ID == id )
                return node;

        return nullptr;
    }

    Link* FindLink( ed::LinkId id )
    {
        for( auto& link : m_Links )
            if( link.ID == id )
                return &link;

        return nullptr;
    }

    Pin* FindPin( ed::PinId id )
    {
        if( !id )
            return nullptr;

        for( auto node : m_Nodes )
        {
            for( auto& pin : node->Inputs )
                if( pin.ID == id )
                    return &pin;

            for( auto& pin : node->Outputs )
                if( pin.ID == id )
                    return &pin;
        }

        return nullptr;
    }

    bool IsPinLinked( ed::PinId id )
    {
        if( !id )
            return false;

        for( auto& link : m_Links )
            if( link.StartPinID == id || link.EndPinID == id )
                return true;

        return false;
    }

    bool CanCreateLink( Pin* a, Pin* b )
    {
        if( !a || !b || a == b || a->Kind == b->Kind || a->Node == b->Node || !( b->AcceptsType( a->Type ) || a->AcceptsType( b->Type ) ) )
            return false;

        return true;
    }

    //void DrawItemRect(ImColor color, float expand = 0.0f)
    //{
    //    ImGui::GetWindowDrawList()->AddRect(
    //        ImGui::GetItemRectMin() - ImVec2(expand, expand),
    //        ImGui::GetItemRectMax() + ImVec2(expand, expand),
    //        color);
    //};

    //void FillItemRect(ImColor color, float expand = 0.0f, float rounding = 0.0f)
    //{
    //    ImGui::GetWindowDrawList()->AddRectFilled(
    //        ImGui::GetItemRectMin() - ImVec2(expand, expand),
    //        ImGui::GetItemRectMax() + ImVec2(expand, expand),
    //        color, rounding);
    //};

    void BuildNode( Node* node )
    {
        for( auto& input : node->Inputs )
        {
            input.Node = node;
            input.Kind = PinKind::Input;
        }

        for( auto& output : node->Outputs )
        {
            output.Node = node;
            output.Kind = PinKind::Output;
        }
    }

    Node* SpawnComment()
    {
        Node* node = new Node( GetNextId(), "Test Comment" );
        m_Nodes.emplace_back( node );
        m_Nodes.back()->Type = NodeType::Comment;
        m_Nodes.back()->Size = ImVec2( 300, 200 );

        return node;
    }

    void BuildNodes()
    {
        for( auto node : m_Nodes )
            BuildNode( node );
    }
    void DrawPinIcon( const Pin& pin, bool connected, int alpha );

    void ShowLeftPane( float paneWidth );

    void HandleAddNodeConxtualMenu();

    void ExportShader(Path& inFile);

    ed::NodeId contextNodeId = 0;
    ed::LinkId contextLinkId = 0;
    ed::PinId  contextPinId = 0;
    bool createNewNode = false;
    Pin* newNodeLinkPin = nullptr;
    Pin* newLinkPin = nullptr;
    SharedPtr<Moonlight::Texture> m_headerTexture;
    SharedPtr<Moonlight::Texture> m_saveTexture;
    SharedPtr<Moonlight::Texture> m_restoreTexture;

    // Temp until I can specify the type of shader.
    BasicShaderMasterNode* m_masterNode = nullptr;

    float leftPaneWidth = 400.0f;
    float rightPaneWidth = 800.0f;
    bool m_isOpen = true;
};