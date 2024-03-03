#pragma once
#include "Core/Node.h"
#include "File.h"

namespace ed = ax::NodeEditor;
namespace util = ax::NodeEditor::Utilities;

class LessThanNode
    : public Node
{
public:
    LessThanNode( int& inId );

    bool OnEvaluate() override;
};

class IntegerNode
    : public Node
{
public:
    int value = 0;
    IntegerNode( int& inId );

    bool OnEvaluate() override;
};


class BasicShaderMasterNode
    : public Node
{
public:
    BasicShaderMasterNode( int& inId );

    virtual bool OnEvaluate() final;
    void ExportShitty( const std::string& inShaderName );
    virtual void OnExport( File& inFile ) final;
    // static? reset on export? move to base?
    int m_variableId = 0;
};