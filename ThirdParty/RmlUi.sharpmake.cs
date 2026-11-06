using System.IO;
using Sharpmake;

// RmlUi (MIT) compiled from the ThirdParty/RmlUi submodule as a static library: Core plus the
// Debugger. The optional Lua, Lottie and SVG plugins stay out (they need lua/rlottie/lunasvg).
[Generate]
public class RmlUiLib : BaseProject
{
    public RmlUiLib()
        : base()
    {
        Name = "RmlUi";
        SourceRootPath = Path.Combine("[project.SharpmakeCsPath]", "RmlUi/Source");
        SourceFilesExcludeRegex.Add(@"[\\/]Source[\\/](Lua|Lottie|SVG)[\\/]");
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Lib;
        conf.SolutionFolder = "ThirdParty";
        conf.ProjectPath = Path.Combine("[project.SharpmakeCsPath]", ".tmp/rmlui");
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "RmlUi/Include"));
        conf.Defines.Add("RMLUI_STATIC_LIB");
        conf.Defines.Add("RMLUI_FONT_ENGINE_FREETYPE");
        // RmlUi's internal profiling zones, routed to Optick / the Linux Chrome trace shim through
        // RmlUiProfiling/tracy/Tracy.hpp (private: the engine's own RmlUi includes see none).
        if (target.Optimization != Sharpmake.Optimization.Retail)
        {
            conf.Defines.Add("RMLUI_TRACY_PROFILING");
            conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "RmlUiProfiling"));
            conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]",
                target.SubPlatform == CommonTarget.SubPlatformType.linux ? "ChromeTrace" : "Optick/src"));
        }
        conf.ExportDefines.Add("RMLUI_STATIC_LIB");
        conf.ExportAdditionalLibrariesEvenForStaticLib = true;

        conf.AddPublicDependency<Freetype>(target);
    }
}
