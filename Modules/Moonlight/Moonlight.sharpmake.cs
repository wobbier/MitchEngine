using System.IO;
using Sharpmake;

[Generate]
public class Moonlight : BaseProject
{
    public Moonlight()
        : base()
    {
        Name = "Moonlight";
        SourceRootPath = Path.Combine("[project.SharpmakeCsPath]", "Source");
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Lib;

        conf.SolutionFolder = "Engine/Modules";


        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/bgfx/include"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/bimg/include"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/bx/include"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/Assimp/include"));

        conf.IncludePaths.Add("[project.SourceRootPath]");

        //conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/Lib/Optick/Win64/[target.Optimization]"));
        //conf.LibraryFiles.Add("OptickCore.lib");

        conf.AddPublicDependency<Dementia>(target, DependencySetting.Default);

        // Can probably remove this once you move the ModelResource metadata out of here.
        conf.AddPublicDependency<ImGui>(target, DependencySetting.Default);
    }

    public override void ConfigureWin64(Configuration conf, CommonTarget target)
    {
        base.ConfigureWin64(conf, target);

        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/bx/include/compat/msvc/"));
    }

    public override void ConfigureMac(Configuration conf, CommonTarget target)
    {
        base.ConfigureMac(conf, target);

        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/bx/include/compat/osx/"));
    }
}