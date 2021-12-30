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
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Lib;

        conf.SolutionFolder = "Engine/Modules";



        //conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/Bullet/src"));
        //conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/glm"));
        //conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/JSON/include"));
        //conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/Optick/src"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/ImGui"));
        conf.IncludePaths.Add("[project.SourceRootPath]");
        conf.AddPublicDependency<Dementia>(target, DependencySetting.Default);

        //conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/Lib/Optick/Win64/[target.Optimization]"));
        //conf.LibraryFiles.Add("OptickCore.lib");
        //conf.IncludePaths.Add(@"[project.SharpmakeCsPath]Source");
    }
}