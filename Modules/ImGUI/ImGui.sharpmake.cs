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
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Lib;

        conf.SolutionFolder = "Modules";



        //conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/Bullet/src"));
        //conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/glm"));
        //conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/JSON/include"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/SDL/include"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui"));
        conf.IncludePaths.Add("[project.SourceRootPath]");
        conf.AddPublicDependency<Dementia>(target, DependencySetting.Default);
        conf.LibraryFiles.Add("ImGui");

        //conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/Lib/Optick/Win64/[target.Optimization]"));
        //conf.LibraryFiles.Add("OptickCore.lib");
        //conf.IncludePaths.Add(@"[project.SharpmakeCsPath]Source");
    }
}