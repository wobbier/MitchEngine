using System.IO;
using Sharpmake;

// FreeType (FTL license) compiled from the ThirdParty/freetype submodule as a static library.
// Sources are the one-line shims in ThirdParty/FreetypeBuild (see its README): the submodule's
// src/ tree can't be globbed directly without compiling include-only parts twice.
[Generate]
public class Freetype : BaseProject
{
    public Freetype()
        : base()
    {
        Name = "Freetype";
        SourceRootPath = Path.Combine("[project.SharpmakeCsPath]", "FreetypeBuild");
        SourceFilesExtensions.Clear();
        SourceFilesExtensions.Add(".c");
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Lib;
        conf.SolutionFolder = "ThirdParty";
        conf.ProjectPath = Path.Combine("[project.SharpmakeCsPath]", ".tmp/freetype");
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "freetype/include"));
        conf.Defines.Add("FT2_BUILD_LIBRARY");
        // Upstream uses the portable C runtime API; MSVC's secure-CRT warnings
        // would otherwise become errors under the shared /WX policy.
        if (target.Platform == Platform.win64)
        {
            conf.Defines.Add("_CRT_SECURE_NO_WARNINGS");
        }
        conf.ExportAdditionalLibrariesEvenForStaticLib = true;
    }

    public override void ConfigureLinux(Configuration conf, CommonTarget target)
    {
        base.ConfigureLinux(conf, target);
        // C sources: drop the C++-only flags.
        conf.AdditionalCompilerOptions.Remove("-frtti");
        conf.AdditionalCompilerOptions.Add("-Wno-maybe-uninitialized");
    }
}
