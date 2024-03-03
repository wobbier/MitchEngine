#define IMGUI_DEFINE_MATH_OPERATORS
#include "Tool.h"
#include "Pointers.h"
#include "ShaderEditorInstance.h"

class ShaderEditor
    : public Tool
{
public:
    ShaderEditor( ToolCreationFlags& inToolCreationFlags );

    void OnStart() override;
    void OnUpdate() override;

private:
    std::vector<SharedPtr<ShaderEditorInstance>> m_editorInstances;
};