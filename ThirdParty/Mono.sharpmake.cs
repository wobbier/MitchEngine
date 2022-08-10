using System.IO;
using Sharpmake;

[Generate]
public class Mono : BaseProject
{
    private readonly string kMONOPath = "C:/Program Files/Mono/include/mono-2.0";
    public Mono()
        : base()
    {
        Name = "Mono";
        SourceRootPath = kMONOPath;;
        //AdditionalSourceRootPaths.Add(Path.Combine(kEASTLPath, "include"));
       // NatvisFiles.Add(Path.Combine(kEASTLPath, "doc", "EASTL.natvis"));
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);

        //conf.TargetPath = Globals.RootDir + "/Engine/.build/[target.Name]/";
        //
        conf.SolutionFolder = "Modules";
        conf.IncludePaths.Add("C:/Program Files/Mono/include/mono-2.0");
        conf.LibraryPaths.Add("C:/Program Files/Mono/lib");
        //conf.IncludePaths.Add(Path.Combine(kEASTLPath, "test", "packages", "EABase", "include", "Common"));
        //conf.Output = Configuration.OutputType.Lib;
        //
        conf.LibraryFiles.Add("mono-2.0-sgen");
        //
        //if (target.Optimization == Optimization.Debug)
        //{
            conf.Defines.Add("MONO_HOME \"C:/Program Files/Mono/\"");
        //}
        //
        //conf.Defines.Add("CHAR16T");
        //conf.Defines.Add("CRT_SECURE_NO_WARNINGS");
        //conf.Defines.Add("SCL_SECURE_NO_WARNINGS");
        //conf.Defines.Add("EASTL_OPENSOURCE=1");
        //conf.Defines.Add("EA_HAVE_CPP11_INITIALIZER_LIST=1");
        //
        //// Disable EA's default implementation of sprintf
        //conf.Defines.Add("EASTL_EASTDC_VSNPRINTF=false");
    }
}