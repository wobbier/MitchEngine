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
        // RmlUi's internal profiling zones (layout, styles, geometry), opt-in: generate with
        // ME_RMLUI_ZONES=1 in the environment, then rebuild RmlUi. They go to Optick / the Linux
        // Chrome trace shim through RmlUiProfiling/tracy/Tracy.hpp. Off by default because RmlUi
        // builds a path string for every element it updates or renders while they're on (its
        // own #ifdef RMLUI_TRACY_PROFILING code): ~0.2-0.35 ms a frame in Debug for HeroShooter's
        // HUD even with no capture running (ME-318).
        if (target.Optimization != Sharpmake.Optimization.Retail
            && System.Environment.GetEnvironmentVariable("ME_RMLUI_ZONES") == "1")
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
