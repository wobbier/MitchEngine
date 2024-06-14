#pragma once
#include "Core/Node.h"
#include "File.h"
#include "Graphics/Texture.h"
#include "utilities/builders.h"

namespace ed = ax::NodeEditor;
namespace util = ax::NodeEditor::Utilities;

// this can be baked into the base? just do checks for all types
struct NumericPin
    : public Pin
{
    NumericPin( int id, const char* name )
        : Pin( id, name, PinType::Numeric )
    {
    }

    virtual bool AcceptsType( PinType inType )
    {
        return true;
    }
};

class LessThanNode
    : public Node
{
public:
    LessThanNode( int& inId );

    bool OnEvaluate() override;
};


class AbsoluteNode
    : public Node
{
public:
    // is this needed?
    float value = 0.f;
    AbsoluteNode( int& inId );

    bool OnEvaluate() override;
    bool OnRender() override;
    void OnExport( ShaderWriter& inFile ) override;
};


class BoolNode
    : public Node
{
public:
    bool value = false;
    BoolNode( int& inId );

    bool OnEvaluate() override;
    bool OnRender() override;
    void OnExport( ShaderWriter& inFile ) override;
};

class IntegerNode
    : public Node
{
public:
    int value = 0;
    IntegerNode( int& inId );

    bool OnEvaluate() override;
    bool OnRender() override;
    void OnExport( ShaderWriter& inFile ) override;
};

class FloatNode
    : public Node
{
public:
    float value = 0.f;
    FloatNode( int& inId );

    bool OnEvaluate() override;
    bool OnRender() override;
    void OnExport( ShaderWriter& inFile ) override;
};

class Vector3Node
    : public Node
{
public:
    Vector3 value;
    Vector3Node( int& inId );

    bool OnEvaluate() override;
    bool OnRender() override;
    void OnExport( ShaderWriter& inFile ) override;
};

using ValueVariant = std::variant<float, Vector2, Vector3, Vector4>;

class AddNode
    : public Node
{
public:
    ValueVariant valueA;
    ValueVariant valueB;
    AddNode( int& inId );

    bool OnEvaluate() override;
    bool OnRender() override;
    void OnExport( ShaderWriter& inFile ) override;
};

class SampleTextureNode
    : public Node
{
public:
    SharedPtr<Moonlight::Texture> value;
    Path filePath;
    SampleTextureNode( int& inId );

    bool OnEvaluate() override;
    bool OnRender() override;
    void OnExport( ShaderWriter& inFile ) override;
    void OnSave( json& outJson, json& rootJson ) override;
};


class CommentNode
    : public Node
{
public:
    std::string Title;
    CommentNode( int& inId );

    void OnSave( json& outJson, json& rootJson ) override;
};



class BasicShaderMasterNode
    : public Node
{
public:
    BasicShaderMasterNode( int& inId );

    virtual bool OnEvaluate() final;
    void ExportShitty( Path& inPath, const std::string& inShaderName );
    virtual void OnExport( ShaderWriter& inFile ) final;

    void ExportPin( int inPinNum, PinType inPinType );
    // static? reset on export? move to base?
    int m_variableId = 0;
};