using System.IO;
using System.Linq;
using Sharpmake;
using static Sharpmake.Options;

[Generate]
public class ScriptCore : CSharpProject
{
    public ScriptCore()
        : base(typeof(CommonTarget))
    {
        Name = "ScriptCore";

        SourceRootPath = @"[project.SharpmakeCsPath]\Source";

        string[] ignoredAssets = { ".xml", ".map", ".config", ".bat", ".txt", ".xsd", ".h.template", ".resx", ".cur" };
        NoneExtensions.Remove(ignoredAssets);

        SourceFilesExtensions = new Strings("cs");

        ProjectSchema = CSharpProjectSchema.NetCore;

        // SDK-style projects need an explicit <Configurations> or VS falls back to AnyCPU.
        // #TODO: this + NetCore schema + unsafe blocks + ignoredAssets is duplicated in GameScript.
        //        factor the shared managed-project setup into a common base when there's a third one.
        CustomProperties.Add("Configurations", GetConfigurationsList());
        CustomProperties.Add("Platforms", "x64");

        AddTargets(CommonTarget.GetDefaultTargets());
    }

    [Configure]
    public virtual void ConfigureAll(Configuration conf, CommonTarget target)
    {
        conf.Output = Configuration.OutputType.DotNetClassLibrary;
        conf.ProjectFileName = @"[project.Name]";
        conf.SolutionFolder = "Engine";

        conf.TargetPath = "$(SolutionDir).build/[target.Name]/";
        conf.ProjectPath = @"[project.SharpmakeCsPath]\.tmp\project";

        conf.Options.Add(Options.CSharp.DebugSymbols.Enabled);
        conf.Options.Add(Options.CSharp.DebugType.Pdbonly);
        conf.Options.Add(Options.CSharp.AllowUnsafeBlocks.Enabled);
    }

    // built from the targets Sharpmake expands, so it stays in sync.
    public static string GetConfigurationsList()
    {
        var names = new System.Collections.Generic.HashSet<string>();
        var optimizations = new[] { Optimization.Debug, Optimization.Release, Optimization.Retail };

        foreach (var t in CommonTarget.GetDefaultTargets())
        {
            foreach (var opt in optimizations)
            {
                if ((t.Optimization & opt) == 0) continue;

                string name = t.SelectedMode == CommonTarget.Mode.Editor
                    ? $"{t.SelectedMode}_{opt}"
                    : $"{t.SelectedMode}_{t.SubPlatform}_{opt}";
                names.Add(name);
            }
        }

        return string.Join(";", names.OrderBy(n => n));
    }
}


[Generate]
public abstract class GameScript : CSharpProject
{
    public GameScript()
        : base(typeof(CommonTarget))
    {
        Name = "Game.Script";
        // SharpmakeCsPath is the subclass's dir (Game.sharpmake.cs), which sits next to Assets.
        SourceRootPath = Path.Combine(SharpmakeCsPath, "Assets");

        string[] ignoredAssets = { ".xml", ".map", ".config", ".bat", ".txt", ".xsd", ".h.template", ".resx", ".cur" };
        NoneExtensions.Remove(ignoredAssets);

        SourceFilesExtensions = new Strings("cs");

        ContentExtension.Add("GenerateSolution.bat", "macOS.yml", "Windows.yml", "UWP.yml");

        ProjectSchema = CSharpProjectSchema.NetCore;
        CustomProperties.Add("Configurations", ScriptCore.GetConfigurationsList());
        CustomProperties.Add("Platforms", "x64");

        AddTargets(CommonTarget.GetDefaultTargets());
    }

    [Configure]
    public virtual void ConfigureAll(Configuration conf, CommonTarget target)
    {
        conf.Output = Configuration.OutputType.DotNetClassLibrary;
        conf.ProjectFileName = @"[project.Name]_[target.Platform]";
        conf.SolutionFolder = "Apps/Game";

        conf.ProjectPath = Path.Combine(SharpmakeCsPath, ".tmp/project/GameScript");
        conf.TargetPath = "$(SolutionDir).build/[target.Name]/";
        conf.AddPublicDependency<ScriptCore>(target);
    }
}
