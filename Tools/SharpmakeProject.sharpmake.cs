using Sharpmake;

public class SharpmakeProjectBase : CSharpProject
{
    public SharpmakeProjectBase()
        : base(typeof(CommonTarget))
    {
        Name = "SharpmakeProject";
        RootPath = @"[project.SharpmakeCsPath]";
        SourceRootPath = @"[project.SharpmakeCsPath]";

        ProjectSchema = CSharpProjectSchema.NetCore;
        CustomProperties.Add("Configurations", ScriptCore.GetConfigurationsList());
        CustomProperties.Add("Platforms", "x64");

        SourceFilesExcludeRegex.Add(@".*\.xml");
        SourceFilesExcludeRegex.Add(@".*\.map");
        SourceFilesExcludeRegex.Add(@".*\.config");
        SourceFilesExcludeRegex.Add(@".*\.bat");
        SourceFilesExcludeRegex.Add(@".*\.txt");
        SourceFilesExcludeRegex.Add(@".*\.xsd");
        SourceFilesExcludeRegex.Add(@".*\.h\.template");
        SourceFilesExcludeRegex.Add(@".*\.template");
        SourceFilesExcludeRegex.Add(@".*\.resx");
        SourceFilesExcludeRegex.Add(@".*\.cur");
        SourceFilesExcludeRegex.Add(@".*\.proto");
        SourceFilesExcludeRegex.Add(@".*\.vsct");
        SourceFilesExcludeRegex.Add(@".*\.settings");
        SourceFilesExcludeRegex.Add(@".*\.manifest");

        SourceFilesExtensions = new Strings("Engine.sharpmake.cs");

        ContentExtension.Add("GenerateSolution.bat", "macOS.yml", "Windows.yml", "UWP.yml");

        SourceFiles.Add(@"[project.SharpmakeCsPath]/Game.sharpmake.cs");
        SourceFiles.Add(@"[project.SharpmakeCsPath]/Engine/Modules/Dementia/Dementia.sharpmake.cs");
        SourceFiles.Add(@"[project.SharpmakeCsPath]/Engine/Modules/Havana/Havana.sharpmake.cs");
        SourceFiles.Add(@"[project.SharpmakeCsPath]/Engine/Modules/Moonlight/Moonlight.sharpmake.cs");
        SourceFiles.Add(@"[project.SharpmakeCsPath]/Engine/Modules/ImGui/ImGui.sharpmake.cs");
        SourceFiles.Add(@"[project.SharpmakeCsPath]/Engine/Modules/ScriptCore/ScriptCore.sharpmake.cs");
        SourceFiles.Add(@"[project.SharpmakeCsPath]/Engine/Tools/BaseProject.sharpmake.cs");
        SourceFiles.Add(@"[project.SharpmakeCsPath]/Engine/Tools/CommonTarget.sharpmake.cs");
        SourceFiles.Add(@"[project.SharpmakeCsPath]/Engine/Tools/SharpmakeProject.sharpmake.cs");
        SourceFiles.Add(@"[project.SharpmakeCsPath]/Engine/Tools/BaseTool.sharpmake.cs");
        SourceFiles.Add(@"[project.SharpmakeCsPath]/Engine/Tools/HUB/MitchHub.sharpmake.cs");
        SourceFiles.Add(@"[project.SharpmakeCsPath]/Engine/Tools/ShaderEditor/ShaderEditor.sharpmake.cs");
        SourceFiles.Add(@"[project.SharpmakeCsPath]/Engine/ThirdParty/Mono.sharpmake.cs");
        //SourceFilesCompileExtensions.Clear();
        //SourceFilesCompileExtensions.Add(".cs");
        DependenciesCopyLocal = DependenciesCopyLocalTypes.None;
        AddTargets(CommonTarget.GetDefaultTargets());
        //SourceFilesBuildExclude.Add("Game.sharpmake.cs");

        //PublicResourceFiles.Clear();
        //ContentExtension.Remove(contentExtension);
        //ResourceFilesExtensions.Remove(contentExtension);
        //EmbeddedResourceExtensions.Remove(contentExtension);
    }

    [Configure]
    public virtual void ConfigureAll(Configuration conf, CommonTarget target)
    {
        conf.Output = Configuration.OutputType.DotNetClassLibrary;
        conf.ProjectFileName = @"[project.Name]_[target.Platform]";
        conf.SolutionFolder = "Tools";

        conf.TargetPath = "$(SolutionDir).build/Sharpmake/[target.Optimization]/";
        conf.ProjectPath = @"[project.SharpmakeCsPath]/.tmp/project/[target.Framework]";
        CSharpProjectExtensions.AddAspNetReferences(conf);
        conf.ReferencesByPath.Add(@"[project.SharpmakeCsPath]/Engine/Tools/Sharpmake/Sharpmake.dll");
        conf.ReferencesByPath.Add(@"[project.SharpmakeCsPath]/Engine/Tools/Sharpmake/Sharpmake.Generators.dll");
    }
}
