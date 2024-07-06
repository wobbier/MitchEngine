#include "CoreNodes.h"

UniformNode::UniformNode( int& inId )
    : Node( inId++, "Uniform" )
{
    Size = {100, 0};
    Outputs.push_back( Pin( inId++, "Value", PinType::Vector4 ) );
    Outputs.back().SetType( PinType::Vector4 );
}

bool UniformNode::OnEvaluate()
{
    return true;
}

void UniformNode::OnExport( ShaderWriter& inFile )
{
    inFile.LastType = PinType::Vector4;
    inFile.LastVariable = "u_viewRect";
}

bool UniformNode::OnRender()
{
    if( ImGui::BeginCombo( "ShitMenu", "View Rect") )
    {
        ImGui::EndCombo();
    }
    return true;
}

