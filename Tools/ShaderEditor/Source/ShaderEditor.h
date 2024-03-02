#include "Tool.h"

class ShaderEditor
    : public Tool
{
public:
    ShaderEditor( ToolCreationFlags& inToolCreationFlags );
    void OnStart() override;


    void OnUpdate() override;

};