using System.IO;
using Sharpmake;

// Box3D (Erin Catto, MIT) compiled from the ThirdParty/box3d submodule as a static C17 library.
[Generate]
public class Box3D : BaseProject
{
    public Box3D()
        : base()
    {
        Name = "Box3D";
        SourceRootPath = Path.Combine("[project.SharpmakeCsPath]", "box3d/src");
        SourceFilesExtensions.Add(".c");
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Lib;
        conf.SolutionFolder = "ThirdParty";
        conf.ProjectPath = Path.Combine("[project.SharpmakeCsPath]", ".tmp/box3d");
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "box3d/include"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "box3d/src"));
        conf.ExportAdditionalLibrariesEvenForStaticLib = true;
    }

    public override void ConfigureWin64(Configuration conf, CommonTarget target)
    {
        base.ConfigureWin64(conf, target);
        conf.AdditionalCompilerOptions.Add("/std:c17");
        // MSVC needs /arch:AVX2 on the wide kernels; keep the portable SSE2 path instead.
        conf.Defines.Add("BOX3D_DISABLE_AVX2");
    }

    public override void ConfigureMac(Configuration conf, CommonTarget target)
    {
        base.ConfigureMac(conf, target);
        conf.AdditionalCompilerOptions.Add("-std=gnu17");
        conf.AdditionalCompilerOptions.Add("-ffp-contract=off");
    }

    public override void ConfigureLinux(Configuration conf, CommonTarget target)
    {
        base.ConfigureLinux(conf, target);
        // C sources: drop the C++-only flags, and keep the solver optimized in debug builds too
        // (an unoptimized physics step makes debugging gameplay painful).
        conf.AdditionalCompilerOptions.Remove("-frtti");
        conf.AdditionalCompilerOptions.Remove("-O0");
        conf.AdditionalCompilerOptions.Remove("-fno-inline-functions");
        conf.AdditionalCompilerOptions.Add("-std=gnu17");
        conf.AdditionalCompilerOptions.Add("-O2");
        conf.AdditionalCompilerOptions.Add("-ffp-contract=off");
        conf.AdditionalCompilerOptions.Add("-Wno-missing-field-initializers");
        conf.AdditionalCompilerOptions.Add("-Wno-unused-value");
        conf.AdditionalCompilerOptions.Add("-Wno-maybe-uninitialized");
    }
}
