using System.IO;
using Sharpmake;
using static Sharpmake.Options;

[Generate]
public class ScriptCore : CSharpProject
{
    public ScriptCore()
        : base(typeof(CommonTarget))
    {
        Name = "ScriptCore";
        SourceRootPath = Path.Combine(Globals.RootDir, "Engine/Modules/ScriptCore/Source");
        AdditionalSourceRootPaths.Add(Path.Combine(Globals.RootDir, "Assets"));

        string[] ignoredAssets = { ".xml", ".map", ".config", ".bat", ".txt", ".xsd", ".h.template", ".resx", ".cur" };
        NoneExtensions.Remove(ignoredAssets);

        SourceFilesExtensions = new Strings("cs");

        ProjectSchema = CSharpProjectSchema.NetFramework;

        AddTargets(CommonTarget.GetDefaultTargets());
    }

    [Configure]
    public virtual void ConfigureAll(Configuration conf, CommonTarget target)
    {
        conf.Output = Configuration.OutputType.DotNetClassLibrary;
        conf.ProjectFileName = @"[project.Name]";
        conf.SolutionFolder = "Engine";

        conf.TargetPath = "$(SolutionDir).build/[target.Name]/";
        conf.ProjectPath = @"[project.SharpmakeCsPath]/.tmp/project/[target.Framework]";
        CSharpProjectExtensions.AddAspNetReferences(conf);
        //conf.ReferencesByPath.Add(@"[project.SharpmakeCsPath]/Engine/Tools/Sharpmake/Sharpmake.dll");
        //conf.ReferencesByPath.Add(@"[project.SharpmakeCsPath]/Engine/Tools/Sharpmake/Sharpmake.Generators.dll");
        
        conf.Options.Add(Options.CSharp.DebugSymbols.Enabled);
        conf.Options.Add(Options.CSharp.DebugType.Pdbonly);
        //conf.CustomAdditionalOptions.Add("/p:DebugType=portable");
        //conf.CustomAdditionalOptions.Add("/p:DebugSymbols=true");
    }
}


[Generate]
public abstract class GameScript : CSharpProject
{ 
    public GameScript()
        : base(typeof(CommonTarget))
    {
        Name = "Game.Script";
        SourceRootPath = Path.Combine(Globals.RootDir, "Assets");
        //SourceFiles.Add(@"[project.SharpmakeCsPath]/Assets/Scripts/TestScript.cs");
        string[] ignoredAssets = { ".xml", ".map", ".config", ".bat", ".txt", ".xsd", ".h.template", ".resx", ".cur" };
        NoneExtensions.Remove(ignoredAssets);

        SourceFilesExtensions = new Strings("cs");

        ContentExtension.Add("GenerateSolution.bat", "macOS.yml", "Windows.yml", "UWP.yml");

        ProjectSchema = CSharpProjectSchema.NetFramework;
        AddTargets(CommonTarget.GetDefaultTargets());
    }

    [Configure]
    public virtual void ConfigureAll(Configuration conf, CommonTarget target)
    {
        conf.Output = Configuration.OutputType.DotNetClassLibrary;
        conf.ProjectFileName = @"[project.Name]_[target.Platform]";
        conf.SolutionFolder = "Apps/Game";

        conf.TargetPath = "$(SolutionDir).build/[target.Name]/";
        conf.ReferencesByPath.Add(@"[project.SharpmakeCsPath].build/[target.Name]/ScriptCore.dll");
    }
}
