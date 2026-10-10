using System.IO;
using Sharpmake;

// Recast (navmesh generation), Detour (runtime navmesh + queries) and DetourCrowd (agents with local
// avoidance), Mikko Mononen et al. (zlib), compiled from the ThirdParty/recastnavigation submodule
// as one static C++ library.
[Generate]
public class RecastNavigation : BaseProject
{
    public RecastNavigation()
        : base()
    {
        Name = "RecastNavigation";
        SourceRootPath = Path.Combine("[project.SharpmakeCsPath]", "recastnavigation/Recast/Source");
        AdditionalSourceRootPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "recastnavigation/Detour/Source"));
        AdditionalSourceRootPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "recastnavigation/DetourCrowd/Source"));
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Lib;
        conf.SolutionFolder = "ThirdParty";
        conf.ProjectPath = Path.Combine("[project.SharpmakeCsPath]", ".tmp/recastnavigation");
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "recastnavigation/Recast/Include"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "recastnavigation/Detour/Include"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "recastnavigation/DetourCrowd/Include"));
        conf.ExportAdditionalLibrariesEvenForStaticLib = true;
    }

    public override void ConfigureLinux(Configuration conf, CommonTarget target)
    {
        base.ConfigureLinux(conf, target);
        // Keep baking fast in debug builds too: an unoptimized Recast build takes seconds per tile.
        conf.AdditionalCompilerOptions.Remove("-O0");
        conf.AdditionalCompilerOptions.Remove("-fno-inline-functions");
        conf.AdditionalCompilerOptions.Add("-O2");
        conf.AdditionalCompilerOptions.Add("-Wno-class-memaccess");
    }

    public override void ConfigureMac(Configuration conf, CommonTarget target)
    {
        base.ConfigureMac(conf, target);
        conf.AdditionalCompilerOptions.Add("-O2");
    }
}
