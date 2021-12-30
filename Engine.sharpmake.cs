using System;
using System.Collections.Generic;
using System.IO;
using Sharpmake;

[module: Sharpmake.Include("Modules/Dementia/Dementia.sharpmake.cs")]
[module: Sharpmake.Include("Modules/ImGui/ImGui.sharpmake.cs")]
[module: Sharpmake.Include("Modules/Moonlight/Moonlight.sharpmake.cs")]
[module: Sharpmake.Include("Tools/CommonTarget.sharpmake.cs")]

public abstract class BaseProject : Project
{
    public BaseProject()
        : base(typeof(CommonTarget))
    {
        Name = "BaseProject";
        SourceRootPath = @"Source";
        IsFileNameToLower = false;
        IsTargetFileNameToLower = false;
        AddTargets(CommonTarget.GetDefaultTargets());
    }

    [Configure]
    public virtual void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        conf.ProjectPath = Path.Combine("[project.SharpmakeCsPath]", ".tmp/project");

        conf.Options.Add(Options.Vc.Compiler.CppLanguageStandard.CPP17);
        conf.Options.Add(Sharpmake.Options.Vc.Compiler.RTTI.Enable);
        conf.Options.Add(Sharpmake.Options.Vc.General.CharacterSet.Unicode);
        conf.Options.Add(Sharpmake.Options.Vc.Compiler.Exceptions.Enable);

        conf.ProjectFileName = @"[project.Name]_[target.Framework]";
        conf.TargetPath = "$(SolutionDir).build/[target.Name]/";
        conf.LibraryPaths.Add("$(OutputPath)");
        conf.LibraryPaths.Add("$(SolutionDir).build/[target.Name]/");

        if (target.SelectedMode == CommonTarget.Mode.Editor)
        {
            conf.Defines.Add("ME_EDITOR");
        }
        if (target.Platform.IsPC())
        {
            conf.Defines.Add("ME_PLATFORM_WIN64");
        }
        conf.Defines.Add("NOMINMAX");

        conf.Options.Add(
            new Options.Vc.Compiler.DisableSpecificWarnings(
                "4201",
                "4100"
                )
        );
    }
}

public abstract class BaseGameProject : BaseProject
{
    public BaseGameProject()
        : base()
    {
        SourceRootPath = @"Game/Source";
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);

        conf.Output = Configuration.OutputType.Dll;

        conf.IncludePaths.Add("$(SolutionDir)Engine/Source");
        conf.IncludePaths.Add("$(SolutionDir)Engine/Modules/Singleton/Source");
        conf.SolutionFolder = "Game";
        //conf.LibraryFiles.Add("Singleton.lib");

    }
}

[Generate]
public class Engine : BaseProject
{
    public Engine()
        : base()
    {
        Name = "Engine";
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Lib;
        conf.SolutionFolder = "Engine";

        conf.IncludePaths.Add("$(SolutionDir)Engine/Source");
        conf.IncludePaths.Add("$(SolutionDir)Engine/Modules/Singleton/Source");
        conf.LibraryFiles.Add("Dementia.lib");

        conf.AddPublicDependency<Dementia>(target, DependencySetting.Default);
    }
}

public class SharpmakeProjectBase : CSharpProject
{
    public SharpmakeProjectBase()
        : base(typeof(CommonTarget))
    {
        Name = "SharpmakeProject";
        SourceRootPath = @"./";

        ProjectSchema = CSharpProjectSchema.NetFramework;
        string[] things = { ".xml", ".map", ".config", ".bat", ".txt", ".xsd", ".template", ".resx", ".cur" };
        NoneExtensions.Remove(things);
        SourceFilesExtensions = new Strings(".sharpmake.cs");
        ContentExtension.Add("GenerateSolution.bat", "macOS.yml", "Windows.yml");
        //SourceFilesCompileExtensions.Clear();
        //SourceFilesCompileExtensions.Add(".cs");
        DependenciesCopyLocal = DependenciesCopyLocalTypes.None;
        AddTargets(CommonTarget.GetDefaultTargets());


        //PublicResourceFiles.Clear();
        //ContentExtension.Remove(contentExtension);
        //ResourceFilesExtensions.Remove(contentExtension);
        //EmbeddedResourceExtensions.Remove(contentExtension);
    }

    [Configure]
    public virtual void ConfigureAll(Configuration conf, CommonTarget target)
    {
        conf.Output = Configuration.OutputType.DotNetClassLibrary;
        conf.ProjectFileName = @"[project.Name]_[target.Framework]";
        conf.SolutionFolder = "Game/Tools";

        conf.TargetPath = "$(SolutionDir).build/Sharpmake/[target.Optimization]/";
        conf.ProjectPath = @"[project.SharpmakeCsPath]/.tmp/project/[target.Framework]";
        CSharpProjectExtensions.AddAspNetReferences(conf);
        conf.ReferencesByPath.Add(@"[project.SharpmakeCsPath]/Engine/Tools/Sharpmake/Sharpmake.dll");
    }
}

public class BaseGameSolution : Solution
{
    public BaseGameSolution()
        : base(typeof(CommonTarget))
    {
        Name = "BaseGameSolution";

        AddTargets(CommonTarget.GetDefaultTargets());

        IsFileNameToLower = false;
    }

    [Configure]
    public virtual void ConfigureAll(Solution.Configuration conf, CommonTarget target)
    {
        conf.SolutionPath = @"[solution.SharpmakeCsPath]\";
        conf.SolutionFileName = "[solution.Name]";

        conf.AddProject<Dementia>(target);
        conf.AddProject<ImGui>(target);
        conf.AddProject<Moonlight>(target);
        //conf.AddProject<Engine>(target);

        conf.AddProject<UserSharpmakeProject>(target);

        //conf.AddProject<SharpGameProject>(target);
    }
}

public static class Main
{
    [Sharpmake.Main]
    public static void SharpmakeMain(Sharpmake.Arguments arguments)
    {
        KitsRootPaths.SetUseKitsRootForDevEnv(DevEnv.vs2019, KitsRootEnum.KitsRoot10, Options.Vc.General.WindowsTargetPlatformVersion.v10_0_19041_0);

        arguments.Generate<SharpGameSolution>();
    }
}