#pragma once
#include "Editor\imgui_node_editor.h"
#include <string>
#include "imgui.h"

namespace ed = ax::NodeEditor;
namespace util = ax::NodeEditor::Utilities;

enum class PinType
{
    Flow,
    Bool,
    Int,
    Float,
    String,
    Object,
    Function,
    Delegate,
};

enum class PinKind
{
    Output,
    Input
};

enum class NodeType
{
    Blueprint,
    Simple,
    Tree,
    Comment,
    Houdini
};

struct Node;

struct Pin
{
    ed::PinId   ID;
    ::Node* Node;
    std::string Name;
    PinType     Type;
    PinKind     Kind;

    Pin( int id, const char* name, PinType type ) :
        ID( id ), Node( nullptr ), Name( name ), Type( type ), Kind( PinKind::Input )
    {
    }
};

struct Node
{
    ed::NodeId ID;
    std::string Name;
    std::vector<Pin> Inputs;
    std::vector<Pin> Outputs;
    ImColor Color;
    NodeType Type;
    ImVec2 Size;

    std::string State;
    std::string SavedState;

    Node( int id, const char* name, ImColor color = ImColor( 255, 255, 255 ) ) :
        ID( id ), Name( name ), Color( color ), Type( NodeType::Blueprint ), Size( 0, 0 )
    {
    }
};

struct Link
{
    ed::LinkId ID;

    ed::PinId StartPinID;
    ed::PinId EndPinID;

    ImColor Color;

    Link( ed::LinkId id, ed::PinId startPinId, ed::PinId endPinId ) :
        ID( id ), StartPinID( startPinId ), EndPinID( endPinId ), Color( 255, 255, 255 )
    {
    }
};

struct NodeIdLess
{
    bool operator()( const ed::NodeId& lhs, const ed::NodeId& rhs ) const
    {
        return lhs.AsPointer() < rhs.AsPointer();
    }
};
