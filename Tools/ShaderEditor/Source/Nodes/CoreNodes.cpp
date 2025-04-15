#include "CoreNodes.h"

UniformNode::UniformNode( int& inId )
    : Node( inId++, "Uniform" )
{
    Size = { 100, 0 };
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
    ImGui::SetCursorPos( ImGui::GetCursorScreenPos() );
    ImGui::PushID( 98527435 );
    if( ImGui::BeginCombo( "ShitMenu2", "View Rect" ) )
    {
        ImGui::Text( "lmao" );
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return true;
}


DeltaTimeNode::DeltaTimeNode( int& inId )
    : Node( inId++, "DeltaTimeNode" )
{
    Size = { 100, 0 };
    Outputs.push_back( Pin( inId++, "Value", PinType::Float ) );
    Outputs.back().SetType( PinType::Float );
}

bool DeltaTimeNode::OnEvaluate()
{
    return true;
}

void DeltaTimeNode::OnExport( ShaderWriter& inFile )
{
    inFile.LastType = PinType::Float;
    inFile.LastVariable = "u_deltaTime";
}

bool DeltaTimeNode::OnRender()
{
    ImGui::SetCursorPos( ImGui::GetCursorScreenPos() );
    ImGui::PushID( 98527435 );
    if( ImGui::BeginCombo( "ShitMenu2", "View Rect" ) )
    {
        ImGui::Text( "lmao" );
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return true;
}

