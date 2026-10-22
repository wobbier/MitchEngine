using System.IO;
using Sharpmake;

[Generate]
public class ImGui : BaseProject
{
    public ImGui()
        : base()
    {
        Name = "ImGui";
        SourceRootPath = Path.Combine("[project.SharpmakeCsPath]", "Source");

        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/misc/cpp/imgui_stdlib.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/misc/cpp/imgui_stdlib.cpp"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/imgui.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/imgui.cpp"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/imconfig.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/imgui_demo.cpp"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/imgui_draw.cpp"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/imgui_internal.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/imgui_tables.cpp"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/imgui_widgets.cpp"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/imstb_rectpack.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/imstb_textedit.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/imstb_truetype.h"));

        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/imgui_stacklayout.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/imgui_stacklayout.cpp"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/imgui_stacklayout_internal.h"));

        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/backends/imgui_impl_sdl2.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/backends/imgui_impl_sdl2.cpp"));

        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGuizmo/src/ImGuizmo.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGuizmo/src/ImGuizmo.cpp"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGuizmo/src/ImSequencer.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGuizmo/src/ImSequencer.cpp"));

        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/imgui-node-editor/imgui_node_editor.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/imgui-node-editor/imgui_node_editor.cpp"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/imgui-node-editor/imgui_node_editor_api.cpp"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/imgui-node-editor/imgui_node_editor_internal.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/imgui-node-editor/imgui_canvas.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/imgui-node-editor/imgui_canvas.cpp"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/imgui-node-editor/crude_json.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/imgui-node-editor/crude_json.cpp"));
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Lib;

        conf.SolutionFolder = "Modules";



        //conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/glm"));
        //conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/JSON/include"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/SDL/include"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGuizmo/src"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/imgui-node-editor"));
        conf.IncludePaths.Add("[project.SourceRootPath]");
        conf.AddPublicDependency<Dementia>(target);
        //conf.LibraryFiles.Add("ImGui");

        //conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/Lib/Optick/Win64/[target.Optimization]"));
        //conf.LibraryFiles.Add("OptickCore.lib");
        //conf.IncludePaths.Add(@"[project.SharpmakeCsPath]Source");
    }
}