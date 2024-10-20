#pragma once
#include "Editor\imgui_node_editor.h"
#include <string>
#include <vector>
#include <variant>
#include "imgui.h"
#include "File.h"
#include "Math\Vector3.h"
#include "ShaderWriter.h"
#include "Pointers.h"
#include "Math\Vector4.h"
#include "Math\Vector2.h"
#include "JSON.h"

namespace ed = ax::NodeEditor;

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
    Texture,
    Vector2,
    Vector3Type,
    Vector4,
    Numeric
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
namespace Moonlight { class Texture; }

using PinData = std::variant<int, bool, float, Vector2, Vector3, Vector4, SharedPtr<Moonlight::Texture>>;

struct Pin
{
    ed::PinId   ID;
    ::Node* Node;
    std::string Name;
    PinType     Type;
    PinType     OGType;
    PinKind     Kind;
    PinData     Data;
    Pin* LinkedInput = nullptr;

    Pin( int id, const char* name, PinType type ) :
        ID( id ), Node( nullptr ), Name( name ), Type( type ), OGType( type ), Kind(PinKind::Input)
    {
    }

    virtual bool AcceptsType( PinType inType ) const
    {
        if (Type == PinType::Numeric)
        {
            return ( inType == PinType::Vector3Type ) || ( inType == PinType::Float );
        }
        return Type == inType;
    }

    // this dont make sense
    PinType GetPinType()
    {
        return LinkedInput ? LinkedInput->Type : Type;
    }

    void SetType( PinType inType )
    {
        Type = inType;
        switch( Type )
        {
        case PinType::Bool:
            Data = false;
            break;
        case PinType::Int:
            Data = 0;
            break;
        case PinType::Float:
            Data = 0.f;
            break;
        case PinType::Texture:
            Data = SharedPtr<Moonlight::Texture>();
            break;
        case PinType::Vector2:
            Data = Vector2();
            break;
        case PinType::Vector3Type:
            Data = Vector3();
            break;
        case PinType::Vector4:
            Data = Vector4();
            break;
        case PinType::Numeric:
            Data = 0.f;
            break;
        default:
            break;
        }
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

    bool TryConvert( PinType inPinType )
    {
        for( auto& input : Inputs )
        {
            if( input.LinkedInput )
            {
                return false;
            }
        }
        for( auto& outputs : Outputs )
        {
            if( outputs.LinkedInput )
            {
                return false;
            }
        }

        for( auto& input : Inputs )
        {
            if( input.Type == PinType::Numeric )
            {
                input.SetType( inPinType );
            }
        }

        // we can't reset outputs atm, since the connection isn't cached
        for( auto& output : Outputs )
        {
            if( output.Type == PinType::Numeric )
            {
                output.SetType( inPinType );
            }
        }

        return true;
    }

    bool TryReset()
    {
        for( auto& input : Inputs )
        {
            if( input.LinkedInput )
            {
                return false;
            }
        }

        for( auto& input : Inputs )
        {
            if( input.Type != input.OGType )
            {
                input.SetType( input.OGType );
            }
        }

        for( auto& output : Outputs )
        {
            if( output.Type != output.OGType )
            {
                output.SetType( output.OGType );
            }
        }

        return true;
    }

    // More of an ensure PinKind method
    void BuildNode()
    {
        for( auto& input : Inputs )
        {
            input.Node = this;
            input.Kind = PinKind::Input;
        }

        for( auto& output : Outputs )
        {
            output.Node = this;
            output.Kind = PinKind::Output;
        }
    }

    virtual bool OnEvaluate()
    {
        return false;
    }

    virtual bool OnRender()
    {
        return false;
    }

    virtual void OnExport( ShaderWriter& inFile )
    {
    }

    bool ExportLinkedPin( int inPinNumber, ShaderWriter& inFile )
    {
        if( Inputs[inPinNumber].LinkedInput )
        {
            Inputs[inPinNumber].LinkedInput->Node->OnExport( inFile );
            return true;
        }
        return false;
    }

    virtual void OnSave( json& outJson, json& rootJson )
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
