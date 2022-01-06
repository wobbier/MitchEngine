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
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/*.cpp"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/*.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/backends/imgui_impl_sdl.h"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/backends/imgui_impl_sdl.cpp"));
        //SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/backends/imgui_impl_dx11.*"));
        //SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui/backends/imgui_impl_win32.*"));
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Lib;

        conf.SolutionFolder = "Engine/Modules";



        //conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/Bullet/src"));
        //conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/glm"));
        //conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/JSON/include"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/SDL/include"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui"));
        conf.IncludePaths.Add("[project.SourceRootPath]");
        conf.AddPublicDependency<Dementia>(target, DependencySetting.Default);
        conf.LibraryFiles.Add("ImGui.lib");

        //conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/Lib/Optick/Win64/[target.Optimization]"));
        //conf.LibraryFiles.Add("OptickCore.lib");
        //conf.IncludePaths.Add(@"[project.SharpmakeCsPath]Source");
    }
}