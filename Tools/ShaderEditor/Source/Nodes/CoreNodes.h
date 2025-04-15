#pragma once
#include "Core/Node.h"
#include "File.h"
#include "Graphics/Texture.h"
#include "utilities/builders.h"

namespace ed = ax::NodeEditor;
namespace util = ax::NodeEditor::Utilities;

class UniformNode
    : public Node
{
public:
    UniformNode( int& inId );

    bool OnEvaluate() override;

    void OnExport( ShaderWriter& inFile ) override;

    bool OnRender() override;
};

class DeltaTimeNode
    : public Node
{
public:
    DeltaTimeNode( int& inId );

    bool OnEvaluate() override;

    void OnExport( ShaderWriter& inFile ) override;

    bool OnRender() override;
};
