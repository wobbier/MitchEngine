using System.IO;
using Sharpmake;

[Generate]
public class Havana : BaseProject
{
    public Havana()
        : base()
    {
        Name = "Havana";
        SourceRootPath = Path.Combine("[project.SharpmakeCsPath]", "Source");
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Exe;

        conf.SolutionFolder = "Apps";

        conf.IncludePaths.Add("[project.SourceRootPath]");

        //conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/Lib/Optick/Win64/[target.Optimization]"));
        //conf.LibraryFiles.Add("OptickCore.lib");

        conf.AddPublicDependency<Dementia>(target, DependencySetting.Default);
        conf.AddPublicDependency<Moonlight>(target, DependencySetting.Default);
        conf.AddPublicDependency<Engine>(target, DependencySetting.Default);

        // Can probably remove this once you move the ModelResource metadata out of here.
        conf.AddPublicDependency<ImGui>(target, DependencySetting.Default);
        conf.AddPublicDependency<SharpGameProject>(target, DependencySetting.Default);
    }
}