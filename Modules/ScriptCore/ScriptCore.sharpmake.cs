using System.IO;
using Sharpmake;

[Generate]
public class ScriptCore : CSharpProject
{
    public ScriptCore()
        : base(typeof(CommonTarget))
    {
        Name = "ScriptCore";
        SourceRootPath = Path.Combine(Globals.RootDir, "Engine/Modules/ScriptCore/Source");
        AdditionalSourceRootPaths.Add(Path.Combine(Globals.RootDir, "Assets"));

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
    }
}


[Generate]
public abstract class GameScript : CSharpProject
{ 
    public GameScript()
        : base(typeof(CommonTarget))
    {
        Name = "Game.Script";
        SourceRootPath = @"Assets";
        //SourceFiles.Add(@"[project.SharpmakeCsPath]/Assets/Scripts/TestScript.cs");

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
