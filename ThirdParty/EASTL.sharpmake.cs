using System.IO;
using Sharpmake;

[Generate]
public class EASTL : BaseProject
{
    private readonly string kEASTLPath = "[project.SharpmakeCsPath]/EASTL";
    public EASTL()
        : base()
    {
        Name = "EASTL";
        SourceRootPath = Path.Combine(kEASTLPath, "source");
        AdditionalSourceRootPaths.Add(Path.Combine(kEASTLPath, "include"));
        NatvisFiles.Add(Path.Combine(kEASTLPath, "doc", "EASTL.natvis"));
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);

        conf.TargetPath = Globals.RootDir + "/Engine/.build/[target.Name]/";

        conf.IncludePaths.Add(Path.Combine(kEASTLPath, "include"));
        conf.IncludePaths.Add(Path.Combine(kEASTLPath, "test", "packages", "EABase", "include", "Common"));
        conf.Output = Configuration.OutputType.Lib;

        conf.LibraryFiles.Add("EASTL");

        if (target.Optimization == Optimization.Debug)
        {
            conf.Defines.Add("DEBUG");
        }

        conf.Defines.Add("CHAR16T");
        conf.Defines.Add("CRT_SECURE_NO_WARNINGS");
        conf.Defines.Add("SCL_SECURE_NO_WARNINGS");
        conf.Defines.Add("EASTL_OPENSOURCE=1");
        conf.Defines.Add("EA_HAVE_CPP11_INITIALIZER_LIST=1");
        conf.Defines.Add("EASTL_EABASE_DISABLED");

        // Disable EA's default implementation of sprintf
        conf.Defines.Add("EASTL_EASTDC_VSNPRINTF=false");
    }
}