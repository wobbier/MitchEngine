using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Sharpmake;

[module: Sharpmake.Include("Modules/Dementia/Dementia.sharpmake.cs")]
[module: Sharpmake.Include("Modules/ImGui/ImGui.sharpmake.cs")]
[module: Sharpmake.Include("Modules/Moonlight/Moonlight.sharpmake.cs")]
[module: Sharpmake.Include("Modules/Havana/Havana.sharpmake.cs")]
[module: Sharpmake.Include("Modules/Player/Player.sharpmake.cs")]
[module: Sharpmake.Include("Modules/ScriptCore/ScriptCore.sharpmake.cs")]
[module: Sharpmake.Include("Tests/Tests.sharpmake.cs")]
[module: Sharpmake.Include("Tools/BaseProject.sharpmake.cs")]
[module: Sharpmake.Include("Tools/CommonTarget.sharpmake.cs")]
[module: Sharpmake.Include("Tools/SharpmakeProject.sharpmake.cs")]
[module: Sharpmake.Include("Tools/BaseTool.sharpmake.cs")]
[module: Sharpmake.Include("ThirdParty/Box2D.sharpmake.cs")]
[module: Sharpmake.Include("ThirdParty/Box3D.sharpmake.cs")]
[module: Sharpmake.Include("ThirdParty/RecastNavigation.sharpmake.cs")]
[module: Sharpmake.Include("ThirdParty/Freetype.sharpmake.cs")]
[module: Sharpmake.Include("ThirdParty/RmlUi.sharpmake.cs")]

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
        conf.Output = Configuration.OutputType.Lib;
        conf.SolutionFolder = "Apps/Game";

        conf.IncludePaths.Add("[project.SourceRootPath]");
        conf.LibraryFiles.Add("[project.Name]");

        conf.AddPublicDependency<Engine>(target);

    }
}

[Generate]
public class EntryPointGameProject : BaseProject
{
    public EntryPointGameProject()
        : base()
    {
        Name = "Game_EntryPoint";

        SourceRootPath = Globals.RootDir + @"/Game_EntryPoint/Source";
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Exe;
        conf.SolutionFolder = "Apps";
        conf.TargetFileName = Globals.ExeName + "_x64";
        if (target.GetPlatform() == Platform.win64)
        {
            conf.ProjectFileName = @"[project.Name]_[target.Platform]";
        }
        conf.TargetPath = Globals.RootDir + "/.build/[target.Name]/";
        conf.VcxprojUserFile.LocalDebuggerWorkingDirectory = Globals.RootDir;
        if (target.SubPlatform == CommonTarget.SubPlatformType.UWP)
        {
            conf.ConsumeWinRTExtensions.Add("main.cpp");
        }

        conf.AddPublicDependency(target, Globals.GameProjectType);
    }

    public override void ConfigureWin64(Configuration conf, CommonTarget target)
    {
        base.ConfigureWin64(conf, target);

        // Copy custom game dlls until I figure this out
        {
            var copyDirBuildStep = new Configuration.BuildStepCopy(
                Path.Combine(Globals.RootDir, $@"ThirdParty/Bin/{CommonTarget.GetThirdPartyOptimization(target.Optimization)}/"),
                Globals.RootDir + "/.build/[target.Name]");

            copyDirBuildStep.IsFileCopy = false;
            copyDirBuildStep.CopyPattern = "*.dll";
            conf.EventPostBuildExe.Add(copyDirBuildStep);
        }
    }
}
[Generate]
public class EntryPointGameProjectUWP : EntryPointGameProject
{
    public EntryPointGameProjectUWP()
        : base()
    {
        Name = "Game_EntryPoint_UWP";
        RootPath = Path.Combine(RootPath, "Game_EntryPoint");
        //ResourceFiles.Add(Globals.RootDir + "/Game_EntryPoint/Assets/*.*");
        //ResourceFiles.Add();
        //ResourceFilesExtensions.Add(".appxmanifest");
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.ProjectPath = Path.Combine(Globals.RootDir, "Game_EntryPoint");
        conf.TargetFileName = Globals.ExeName + "_UWP";

        conf.ConsumeWinRTExtensions.Add("main.cpp");
        conf.AppxManifestFilePath = Path.Combine(Globals.RootDir, "Game_EntryPoint/Package.appxmanifest");
        conf.NeedsAppxManifestFile = true;
        conf.IsUniversalWindowsPlatform = true;
        conf.PackageCertificateKeyFile = Path.Combine(Globals.RootDir, $"Game_EntryPoint/{Globals.UWP_CertificateName}");
        conf.PackageCertificateThumbprint = Globals.UWP_Thumbprint;
        conf.Options.Add(Options.Vc.Linker.SubSystem.Windows);

        conf.Images.Add("Assets/SplashScreen.scale-200.png");
        conf.Images.Add("Assets/LockScreenLogo.scale-200.png");
        conf.Images.Add("Assets/Wide310x150Logo.scale-200.png");
        conf.Images.Add("Assets/Square44x44Logo.scale-200.png");
        conf.Images.Add("Assets/Square44x44Logo.targetsize-24_altform-unplated.png");
        conf.Images.Add("Assets/Square150x150Logo.scale-200.png");
        conf.Images.Add("Assets/StoreLogo.png");
    }
}

[Generate]
public class Engine : BaseProject
{
    public Engine()
        : base()
    {
        Name = "MitchEngine";
        NatvisFiles.Add("Engine.natvis");
        SourceFilesExtensions.Add("mm");
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Lib;
        conf.SolutionFolder = "Engine";

        conf.PrecompHeader = "PCH.h";
        conf.PrecompSource = "PCH.cpp";

        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "Source"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "Modules/Singleton/Source"));

        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/SDL/include"));
        // World-space text (TextRenderer) rasterizes SDF glyphs with FreeType (linked through RmlUi).
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/freetype/include"));

        // TODO: Fix other API's
        //if (target.SubPlatform == CommonTarget.SubPlatformType.Win64 || target.SubPlatform == CommonTarget.SubPlatformType.macOS)

        conf.LibraryFiles.Add("MitchEngine");

        conf.AddPublicDependency<Moonlight>(target);
        conf.AddPublicDependency<Dementia>(target);
        conf.AddPublicDependency<ImGui>(target);
        conf.AddPublicDependency<Box2D>(target);
        conf.AddPublicDependency<Box3D>(target);
        conf.AddPublicDependency<RecastNavigation>(target);
        conf.AddPublicDependency<RmlUiLib>(target);

        // Box3D (3D) and Box2D (2D) physics are built from source and always available.
        if (Globals.IsPhysicsEnabled3D)
        {
            conf.Defines.Add("DEFINE_ME_PHYSICS_3D");
        }

        // #TODO This shouldn't be a sharpmake class
        // Sharpmake only generates C# projects for Windows. Elsewhere the engine builds the SDK-style
        // Modules/ScriptCore/ScriptCore.csproj and the game's Project/Game.Script.csproj with the
        // dotnet CLI (ScriptEngine::BuildScripts).
        if (target.Platform == Platform.win64 && Directory.Exists(Globals.DOTNET_Win64_Dir))
        {
            conf.AddPublicDependency<ScriptCore>(target);
            if (Globals.GameScriptType != null)
            {
                conf.AddPublicDependency(target, Globals.GameScriptType);
            }
        }
    }

    public override void ConfigureWin64(Configuration conf, CommonTarget target)
    {
        base.ConfigureWin64(conf, target);

        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", $"ThirdParty/Lib/Assimp/{CommonTarget.GetThirdPartyOptimization(target.Optimization)}"));
        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", $"ThirdParty/Lib/SDL/Win64/{CommonTarget.GetThirdPartyOptimization(target.Optimization)}"));
        conf.LibraryFiles.Add("winmm.lib");
        conf.LibraryFiles.Add("winhttp.lib");


        // Headless servers (Mode.Server) have no audio: no FMOD to link or ship (ME-287).
        if (Directory.Exists(Globals.FMOD_Win64_Dir) && target.HasAudio)
        {
            conf.IncludePaths.Add(Path.Combine(Globals.FMOD_Win64_Dir, "api/core/inc"));
            conf.LibraryPaths.Add(Path.Combine(Globals.FMOD_Win64_Dir, "api/core/lib/x64"));

            // Retail ships FMOD without its logging build (which writes fmod_log.txt beside the game).
            conf.LibraryFiles.Add(target.Optimization == Optimization.Retail ? "fmod_vc.lib" : "fmodL_vc.lib");

            // FMOD DLL
            {
                var copyDirBuildStep = new Configuration.BuildStepCopy(
                    Path.Combine(Globals.FMOD_Win64_Dir, "api/core/lib/x64"),
                    Globals.RootDir + "/.build/[target.Name]");

                copyDirBuildStep.IsFileCopy = false;
                copyDirBuildStep.CopyPattern = "*.dll";
                conf.EventPostBuildExe.Add(copyDirBuildStep);
            }
        }

        // Do a virtual method for different configs
        if (target.Optimization == Optimization.Debug)
        {
            conf.LibraryFiles.Add("SDL2d.lib");
            conf.LibraryFiles.Add("assimpd.lib");
            conf.LibraryFiles.Add("zlibstaticd.lib");
        }
        else
        {
            conf.LibraryFiles.Add("SDL2.lib");
            conf.LibraryFiles.Add("assimp.lib");
            conf.LibraryFiles.Add("zlibstatic.lib");
        }

        // SDL DLL
        {
            var copyDirBuildStep = new Configuration.BuildStepCopy(
                $@"[project.SharpmakeCsPath]/ThirdParty/Lib/SDL/Win64/{CommonTarget.GetThirdPartyOptimization(target.Optimization)}",
                Globals.RootDir + "/.build/[target.Name]");

            copyDirBuildStep.IsFileCopy = false;
            copyDirBuildStep.CopyPattern = "*.dll";
            conf.EventPostBuildExe.Add(copyDirBuildStep);
        }

        if (Directory.Exists(Globals.DOTNET_Win64_Dir))
        {
            conf.IncludePaths.Add(Globals.DOTNET_Win64_Dir);
            conf.LibraryPaths.Add(Globals.DOTNET_Win64_Dir);
            conf.LibraryFiles.Add("nethost");

            {
                var copyDirBuildStep = new Configuration.BuildStepCopy(
                    Globals.DOTNET_Win64_Dir,
                    Globals.RootDir + "/.build/[target.Name]");
                copyDirBuildStep.IsFileCopy = false;
                copyDirBuildStep.CopyPattern = "nethost.dll";
                conf.EventPostBuildExe.Add(copyDirBuildStep);
            }

            // hostfxr loads this relative to the working dir (root), so copy it out of source
            {
                var copyRuntimeConfig = new Configuration.BuildStepCopy(
                    Path.Combine(Globals.EngineDir, "Source/Scripting/ScriptCore.runtimeconfig.json"),
                    Path.Combine(Globals.RootDir, "ScriptCore.runtimeconfig.json"));
                copyRuntimeConfig.IsFileCopy = true;
                conf.EventPostBuildExe.Add(copyRuntimeConfig);
            }
        }

    }

    public override void ConfigureUWP(Configuration conf, CommonTarget target)
    {
        base.ConfigureUWP(conf, target);

        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", $"ThirdParty/Lib/Assimp/{CommonTarget.GetThirdPartyOptimization(target.Optimization)}"));
        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", $"ThirdParty/Lib/SDL/UWP/{CommonTarget.GetThirdPartyOptimization(target.Optimization)}"));
        conf.LibraryFiles.Add("winmm.lib");
        conf.LibraryFiles.Add("winhttp.lib");

        conf.LibraryPaths.Add("$(VCInstallDir)\\lib\\store\\amd64");
        conf.LibraryPaths.Add("$(VCInstallDir)\\lib\\amd64");

        conf.LibraryFiles.Add("SDL2.lib");
        if (target.Optimization == Optimization.Debug)
        {
            conf.LibraryFiles.Add("assimpd.lib");
            conf.LibraryFiles.Add("zlibstaticd.lib");
        }
        else
        {
            conf.LibraryFiles.Add("assimp.lib");
            conf.LibraryFiles.Add("zlibstatic.lib");
        }

        if (Directory.Exists(Globals.FMOD_UWP_Dir))
        {
            conf.IncludePaths.Add(Path.Combine(Globals.FMOD_UWP_Dir, "api/core/inc"));
            conf.LibraryPaths.Add(Path.Combine(Globals.FMOD_UWP_Dir, "api/core/lib/x64"));

            conf.LibraryFiles.Add(target.Optimization == Optimization.Retail ? "fmod.lib" : "fmodL.lib");

            // FMOD DLL
            {
                var copyDirBuildStep = new Configuration.BuildStepCopy(
                    Path.Combine(Globals.FMOD_UWP_Dir, "api/core/lib/x64"),
                    Globals.RootDir + "/.build/[target.Name]");

                copyDirBuildStep.IsFileCopy = false;
                copyDirBuildStep.CopyPattern = "*.dll";
                conf.EventPostBuildExe.Add(copyDirBuildStep);
            }
        }

    }

    public override void ConfigureMac(Configuration conf, CommonTarget target)
    {
        base.ConfigureMac(conf, target);

        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/Lib/SDL/macOS/Debug"));
        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", $"ThirdParty/Lib/Assimp/macOS/{CommonTarget.GetThirdPartyOptimization(target.Optimization)}"));

        // Build Machine
        if (!Directory.Exists(Globals.FMOD_macOS_Dir) && Directory.Exists(Path.Combine(Globals.EngineDir, "ThirdParty/FMOD")))
        {
            Globals.FMOD_macOS_Dir = Path.Combine(Globals.EngineDir, "ThirdParty/FMOD");
        }


        conf.LibraryFiles.Add("SDL2d");

        if (target.Optimization == Optimization.Debug)
        {
            conf.LibraryFiles.Add("assimpd");
            conf.LibraryFiles.Add("zlibstaticd");
        }
        else
        {
            conf.LibraryFiles.Add("assimp");
            conf.LibraryFiles.Add("zlibstatic");
        }

        if (Directory.Exists(Globals.FMOD_macOS_Dir) && target.HasAudio)
        {
            conf.IncludePaths.Add(Path.Combine(Globals.FMOD_macOS_Dir, "api/core/inc"));
            conf.LibraryPaths.Add(Path.Combine(Globals.FMOD_macOS_Dir, "api/core/lib"));

            conf.LibraryFiles.Add(target.Optimization == Optimization.Retail ? "fmod" : "fmodL");

            // FMOD LIB
            {
                var copyDirBuildStep = new Configuration.BuildStepCopy(
                    Path.Combine(Globals.FMOD_macOS_Dir, "api/core/lib"),
                    Globals.RootDir + "/.build/[target.Name]");

                copyDirBuildStep.IsFileCopy = false;
                copyDirBuildStep.CopyPattern = "*.*";
                conf.EventPostBuildExe.Add(copyDirBuildStep);
            }
        }


    }


    public override void ConfigureLinux(Configuration conf, CommonTarget target)
    {
        base.ConfigureLinux(conf, target);

        if (Directory.Exists(Globals.DOTNET_Linux_Dir))
        {
            System.Console.WriteLine("exists");
            conf.IncludePaths.Add(Globals.DOTNET_Linux_Dir);
            conf.AdditionalLinkerOptions.Add($"-L{Globals.DOTNET_Linux_Dir}");
            conf.AdditionalLinkerOptions.Add($"-Wl,-rpath,{Globals.DOTNET_Linux_Dir}");
            conf.AdditionalLinkerOptions.Add("-lnethost");
            conf.AdditionalLinkerOptions.Add("-ldl");

            // hostfxr_initialize_for_runtime_config expects this in the CWD at runtime
            var copyRuntimeConfig = new Configuration.BuildStepCopy(
                Path.Combine(Globals.EngineDir, "Source/Scripting/ScriptCore.runtimeconfig.json"),
                Path.Combine(Globals.RootDir, "ScriptCore.runtimeconfig.json"));
            copyRuntimeConfig.IsFileCopy = true;
            conf.EventPostBuildExe.Add(copyRuntimeConfig);
        }

        if (Directory.Exists(Globals.FMOD_Linux_Dir) && target.HasAudio)
        {
            conf.IncludePaths.Add(Path.Combine(Globals.FMOD_Linux_Dir, "api/core/inc"));
            conf.LibraryPaths.Add(Path.Combine(Globals.FMOD_Linux_Dir, "api/core/lib/x86_64"));
            conf.AdditionalLinkerOptions.Add(target.Optimization == Optimization.Retail ? "-lfmod" : "-lfmodL");
        }

        // potentially use [target.Platform] and move this to ConfigureAll
        //conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", $"ThirdParty/Lib/SDL/linux/{CommonTarget.GetThirdPartyOptimization(target.Optimization)}"));
        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", $"ThirdParty/Lib/Assimp/linux/Release"));

        // TODO: Release v debug?
        //conf.LibraryFiles.Add("SDL2");
        conf.LibraryFiles.Add("assimp");
        conf.LibraryFiles.Add("zlibstatic");
        // Link against the system-provided copies of the third-party libs we use on Linux.

        // Module dependencies carry the matching Debug/Release bgfx archives into
        // the linker group configured by BaseProject.ConfigureLinux.
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
        conf.SolutionPath = @"[solution.SharpmakeCsPath]";
        conf.SolutionFileName = "[solution.Name]";
        Globals.RootDir = Path.GetFullPath("../");
        Globals.DetectSdks();

        conf.AddProject<Dementia>(target);
        conf.AddProject<ImGui>(target);
        conf.AddProject<Moonlight>(target);
        conf.AddProject<Engine>(target);

        if (target.SelectedMode == CommonTarget.Mode.Editor)
        {
            conf.AddProject<Havana>(target);
        }
        else
        {
            if (target.SubPlatform == CommonTarget.SubPlatformType.UWP)
            {
                conf.AddProject<EntryPointGameProjectUWP>(target);
            }
            else
            {
                // The generic runtime for C#-only projects (Docs/Projects.md).
                conf.AddProject<PlayerProject>(target);
            }
            {
                conf.AddProject<EntryPointGameProject>(target);
            }
        }

        if (target.Platform == Platform.win64)
        {
            conf.AddProject(Globals.GameSharpmakeProjectType, target);
        }

        conf.AddProject(Globals.GameProjectType, target);
        if (target.SubPlatform != CommonTarget.SubPlatformType.UWP)
        {
            conf.AddProject<EngineTests>(target);
        }

        WriteProjectFile(Globals.RootDir + "Project/Game.meproj", Name);
    }

    // Project/Game.meproj names the solution (BuildProject.sh, Build Game and the editor read it).
    // Generation creates it or renames the project in it, leaving every other key alone.
    private static void WriteProjectFile(string path, string projectName)
    {
        string nameEntry = "\"ProjectName\":\"" + projectName + "\"";
        string text = File.Exists(path) ? File.ReadAllText(path) : string.Empty;
        int open = text.IndexOf('{');
        if (open < 0)
        {
            File.WriteAllText(path, "{" + nameEntry + "}");
            return;
        }
        var entry = new System.Text.RegularExpressions.Regex("\"ProjectName\"\\s*:\\s*\"([^\"]*)\"");
        var match = entry.Match(text);
        if (match.Success && match.Groups[1].Value == projectName)
        {
            return;
        }
        bool empty = text.Substring(open + 1).Trim().StartsWith("}");
        File.WriteAllText(path, match.Success ? entry.Replace(text, nameEntry, 1) : text.Insert(open + 1, nameEntry + (empty ? "" : ",")));
    }
}

// The engine checkout's own solution (Docs/Projects.md, "The engine's own solution"): Havana, the
// Player and the tests with no game module, generated by GenerateSolution.{bat,command,sh} in the
// engine root into MitchEngine.sln / MitchEngine.make there. Havana opens the Project Hub and the
// C#-only projects it makes live anywhere. A game checkout generates its Game.sharpmake.cs
// (SharpGameSolution, a BaseGameSolution) instead, and never this.
[Generate]
public class EngineSolution : Solution
{
    public EngineSolution()
        : base(typeof(CommonTarget))
    {
        Name = "MitchEngine";

        // UWP ships from a game's own entry point: the engine's solution has nothing to run there.
        AddTargets(CommonTarget.GetDefaultTargets().Where(t => t.SubPlatform != CommonTarget.SubPlatformType.UWP).ToArray());

        IsFileNameToLower = false;
    }

    [Configure]
    public virtual void ConfigureAll(Solution.Configuration conf, CommonTarget target)
    {
        conf.SolutionPath = @"[solution.SharpmakeCsPath]";
        conf.SolutionFileName = "[solution.Name]";
        Globals.RootDir = Globals.OnDiskSpelling(SharpmakeCsPath).TrimEnd('/', '\\') + Path.DirectorySeparatorChar;
        Globals.DetectSdks();

        conf.AddProject<Dementia>(target);
        conf.AddProject<ImGui>(target);
        conf.AddProject<Moonlight>(target);
        conf.AddProject<Engine>(target);

        // One startup project per .sln (Sharpmake's Visual Studio generator refuses two): Havana,
        // which Visual Studio then also runs in the game configurations' F5 until you pick the Player.
        if (target.SelectedMode == CommonTarget.Mode.Editor)
        {
            conf.AddProject<Havana>(target);
            conf.SetStartupProject<Havana>();
        }
        else
        {
            conf.AddProject<PlayerProject>(target);
        }

        conf.AddProject<EngineTests>(target);
    }
}

[Generate]
public class BaseScriptSolution : Solution
{
    public BaseScriptSolution()
        : base(typeof(CommonTarget))
    {
        Name = "BaseScriptSolution";

        AddTargets(CommonTarget.GetDefaultTargets());

        IsFileNameToLower = false;
    }

    [Configure]
    public virtual void ConfigureAll(Solution.Configuration conf, CommonTarget target)
    {
        conf.SolutionPath = Path.GetFullPath(Path.Combine(SharpmakeCsPath, ".."));
        conf.SolutionFileName = "GameScript";

    }
}

public class Globals
{
    // The checkout being generated: a game's root, or the engine's own (EngineSolution). Builds go
    // to RootDir/.build/.
    public static string RootDir = string.Empty;

    // Where the engine is: the game's Engine/ submodule, or the checkout itself.
    public static string EngineDir => HasGameModule ? Path.Combine(RootDir, "Engine") : RootDir;

    // The game's own types, declared by its Game.sharpmake.cs. Null when the engine generates its
    // own solution, which has no game module (and Havana then links none).
    public static System.Type GameProjectType => FindGameType("SharpGameProject");
    public static System.Type GameScriptType => FindGameType("UserGameScript");
    public static System.Type GameSolutionType => FindGameType("SharpGameSolution");
    public static System.Type GameSharpmakeProjectType => FindGameType("UserSharpmakeProject");
    public static bool HasGameModule => GameProjectType != null;

    private static System.Type FindGameType(string name)
    {
        return typeof(Globals).Assembly.GetType(name);
    }
    public static string FMOD_Win64_Dir = string.Empty;
    public static string FMOD_UWP_Dir = string.Empty;
    public static string FMOD_macOS_Dir = string.Empty;
    public static string FMOD_Linux_Dir = string.Empty;

    public static string ExeName = "Game_EntryPoint";

    // C# Scripting
    // Unused since the move to .NET 8 (kept so older game sharpmake files that set them still build).
    public static string MONO_Win64_Dir = string.Empty; //"C:/Program Files/Mono/";
    public static string MONO_macOS_Dir = string.Empty; //"/Library/Frameworks/Mono.framework/";
    public static string MONO_Linux_Dir = string.Empty; //"/usr/lib/mono/";
    public static string DOTNET_Win64_Dir = string.Empty; //C:\Program Files\dotnet\packs\Microsoft.NETCore.App.Host.win-x64\8.0.22\runtimes\win-x64\native
    public static string DOTNET_Linux_Dir = string.Empty; // set via DOTNET_LINUX_NATIVE_DIR env var in nix develop

    public static string UWP_Thumbprint = "2b58614583c74c71d9068804a758d87346f87f40";
    public static string UWP_CertificateName = "Game_EntryPoint_UWP_TemporaryKey.pfx";

    public static bool IsUWPEnabled = true;
    public static bool IsPhysicsEnabled3D = true;

    // Optional SDKs a game's Game.sharpmake.cs didn't name (or that aren't where it says): the
    // engine's ThirdParty/FMOD on build machines, .NET from the dev shell on Linux, and for the
    // engine's own solution on Windows, FMOD's default install and the newest .NET 8 host pack.
    public static void DetectSdks()
    {
        if (!Directory.Exists(FMOD_Win64_Dir) && Directory.Exists(Path.Combine(EngineDir, "ThirdParty/FMOD")))
            FMOD_Win64_Dir = Path.Combine(EngineDir, "ThirdParty/FMOD");
        if (!Directory.Exists(FMOD_Linux_Dir) && Directory.Exists(Path.Combine(EngineDir, "ThirdParty/FMOD")))
            FMOD_Linux_Dir = Path.Combine(EngineDir, "ThirdParty/FMOD");
        if (!Directory.Exists(FMOD_Linux_Dir) && Directory.Exists(Path.Combine(EngineDir, "ThirdParty/fmod")))
            FMOD_Linux_Dir = Path.Combine(EngineDir, "ThirdParty/fmod");
        // Only for the engine's own solution: a game's Game.sharpmake.cs names its SDKs, and its UWP
        // configurations have no .NET host to build against.
        if (Util.GetExecutingPlatform() == Platform.win64 && !HasGameModule)
        {
            string fmodInstall = "C:/Program Files (x86)/FMOD SoundSystem/";
            if (!Directory.Exists(FMOD_Win64_Dir) && Directory.Exists(fmodInstall + "FMOD Studio API Windows"))
                FMOD_Win64_Dir = fmodInstall + "FMOD Studio API Windows/";
            if (!Directory.Exists(FMOD_UWP_Dir) && Directory.Exists(fmodInstall + "FMOD Studio API Universal Windows Platform"))
                FMOD_UWP_Dir = fmodInstall + "FMOD Studio API Universal Windows Platform/";
            if (!Directory.Exists(DOTNET_Win64_Dir))
                DOTNET_Win64_Dir = FindDotnetHostPack() ?? DOTNET_Win64_Dir;
        }

        string dotnetNativeEnv = System.Environment.GetEnvironmentVariable("DOTNET_LINUX_NATIVE_DIR") ?? string.Empty;
        if (!string.IsNullOrEmpty(dotnetNativeEnv) && Directory.Exists(dotnetNativeEnv))
            DOTNET_Linux_Dir = dotnetNativeEnv;

        ReportFeatures();
    }

    // The generate scripts run Sharpmake from the engine root: prefer the working directory's
    // spelling of it (Sharpmake may hand paths back in another case on Windows).
    public static string OnDiskSpelling(string path)
    {
        string current = Directory.GetCurrentDirectory();
        bool same = string.Equals(Path.GetFullPath(current).TrimEnd('/', '\\'), Path.GetFullPath(path).TrimEnd('/', '\\'), System.StringComparison.OrdinalIgnoreCase);
        return same ? current : path;
    }

    // <dotnet>/packs/Microsoft.NETCore.App.Host.win-x64/<newest 8.x>/runtimes/win-x64/native: nethost
    // for ScriptCore, which targets net8.0.
    private static string FindDotnetHostPack()
    {
        string dotnetRoot = System.Environment.GetEnvironmentVariable("DOTNET_ROOT");
        if (string.IsNullOrEmpty(dotnetRoot))
            dotnetRoot = Path.Combine(System.Environment.GetFolderPath(System.Environment.SpecialFolder.ProgramFiles), "dotnet");
        string packs = Path.Combine(dotnetRoot, "packs", "Microsoft.NETCore.App.Host.win-x64");
        if (!Directory.Exists(packs))
            return null;
        string best = null;
        System.Version bestVersion = null;
        foreach (string pack in Directory.GetDirectories(packs))
        {
            System.Version version;
            if (!System.Version.TryParse(Path.GetFileName(pack), out version) || version.Major != 8)
                continue;
            string native = Path.Combine(pack, "runtimes", "win-x64", "native");
            if (Directory.Exists(native) && (bestVersion == null || version > bestVersion))
            {
                best = native;
                bestVersion = version;
            }
        }
        return best;
    }

    private static bool s_reportedFeatures = false;

    // Optional features follow which SDKs exist at generation time and silently compile out when one
    // is missing, so say what this generation found (once).
    private static void ReportFeatures()
    {
        if (s_reportedFeatures)
        {
            return;
        }
        s_reportedFeatures = true;
        bool windows = System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Windows);
        bool mac = System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.OSX);
        string fmodDir = windows ? Globals.FMOD_Win64_Dir : (mac ? Globals.FMOD_macOS_Dir : Globals.FMOD_Linux_Dir);
        string dotnetDir = windows ? Globals.DOTNET_Win64_Dir : (mac ? string.Empty : Globals.DOTNET_Linux_Dir);
        bool fmod = Directory.Exists(fmodDir);
        bool dotnet = Directory.Exists(dotnetDir);
        string Line(string name, bool on, string detail) => $"  {name,-16} {(on ? "on " : "OFF")}  {detail}";
        System.Console.WriteLine("MitchEngine features for this generation (optional SDKs are detected by directory):");
        System.Console.WriteLine(Line("FMOD audio", fmod, fmod ? fmodDir : "no SDK found (Engine/ThirdParty/FMOD or Globals.FMOD_*_Dir); audio compiles to no-ops"));
        System.Console.WriteLine(Line(".NET scripting", dotnet, dotnet ? dotnetDir
            : (mac ? "no macOS host yet" : (windows ? "install the .NET 8 SDK, or set Globals.DOTNET_Win64_Dir to its Microsoft.NETCore.App.Host.win-x64 native folder" : "DOTNET_LINUX_NATIVE_DIR not set or missing: generate inside `nix develop`"))));
        System.Console.WriteLine(Line("RmlUi game UI", true, "RmlUi + FreeType are built from source"));
        System.Console.WriteLine(Line("Optick", windows, windows ? "Win64 builds (not Retail)" : "Win64 only"));
        System.Console.WriteLine(Line("RenderDoc", windows && File.Exists("C:/Program Files/RenderDoc/renderdoc.dll"), windows ? "C:/Program Files/RenderDoc" : "Windows only"));
        System.Console.WriteLine(Line("Physics, navmesh", true, "Box3D, Box2D and Recast/Detour are built from source"));
    }
}


public static class Main
{
    [Sharpmake.Main]
    public static void SharpmakeMain(Sharpmake.Arguments arguments)
    {
        if (Util.GetExecutingPlatform() == Platform.linux)
        {
            // Workaround for the Sharpmake 0.20.0+ regression (commit e3142832):
            // Linux Makefiles link through $(CXX), so linker-only arguments must be
            // emitted as -Wl,. Without this, the LibGroup flags enabled in
            // BaseProject.ConfigureLinux come out bare and ld's left-to-right archive
            // ordering breaks the link (undefined assimp/bimg_decode references).
            var linuxDescriptor = (Linux.LinuxPlatform)PlatformRegistry.Get<IPlatformDescriptor>(Platform.linux);
            linuxDescriptor.IsLinkerInvokedViaCompiler = true;
        }

        KitsRootPaths.SetUseKitsRootForDevEnv(DevEnv.vs2022, KitsRootEnum.KitsRoot10, Options.Vc.General.WindowsTargetPlatformVersion.v10_0_19041_0);

        // A game checkout's Game.sharpmake.cs declares its solution; the engine's own checkout
        // (GenerateSolution.* in its root, which compiles only this file) has none.
        if (Globals.GameSolutionType != null)
        {
            arguments.Generate(Globals.GameSolutionType);
            arguments.Generate<BaseScriptSolution>();
        }
        else
        {
            arguments.Generate<EngineSolution>();
        }
    }
}
