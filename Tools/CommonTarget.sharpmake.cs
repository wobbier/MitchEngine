using System;
using System.Collections.Generic;
using Sharpmake;

public class CommonTarget : Sharpmake.ITarget
{
    public Platform Platform;
    public DevEnv DevEnv;
    public Optimization Optimization;
    public DotNetFramework Framework;
    public DotNetOS DotNetOS;

    public static Optimization GetThirdPartyOptimization(Optimization opt)
    {
        switch (opt)
        {
            case Optimization.Debug:
                return Optimization.Debug;
            case Optimization.Release:
                return Optimization.Release;
            case Optimization.Retail:
                return Optimization.Release;
            default:
                return Optimization.Release;
        }
    }

    [Fragment, Flags]
    public enum SubPlatformType
    {
        Win64 = 1 << 0,
        macOS = 1 << 1,
        UWP   = 1 << 2,
    }
    public SubPlatformType SubPlatform = SubPlatformType.Win64;

    [Fragment, Flags]
    public enum Mode
    {
        Game = 1 << 0,
        Editor = 1 << 1
    }
    public Mode SelectedMode = Mode.Game;

    public CommonTarget() { }

    public CommonTarget(
        Platform platform,
        DevEnv devEnv,
        Optimization optimization,
        DotNetFramework dotNetFramework,
        DotNetOS dotNetOS
    )
    {
        Platform = platform;
        DevEnv = devEnv;
        Optimization = optimization;
        Framework = dotNetFramework;
        DotNetOS = dotNetOS;
    }

    public static CommonTarget[] GetDefaultTargets()
    {
        switch (Util.GetExecutingPlatform())
        {
            case Platform.win64:
                {
                    var baseTarget = new CommonTarget(
                        Platform.win64,
                        DevEnv.vs2022,
                        Optimization.Debug | Optimization.Release | Optimization.Retail,
                        DotNetFramework.v4_8,
                        dotNetOS: 0);
                    baseTarget.SubPlatform = SubPlatformType.Win64;

                    CommonTarget uwpTarget = null;
                    if (Globals.IsUWPEnabled)
                    {
                        uwpTarget = new CommonTarget(
                            Platform.win64,
                            DevEnv.vs2022,
                            Optimization.Debug | Optimization.Release | Optimization.Retail,
                            DotNetFramework.v4_8,
                            dotNetOS: 0);
                        uwpTarget.SubPlatform = SubPlatformType.UWP;
                    }

                    var editorTarget = new CommonTarget(
                        Platform.win64,
                        DevEnv.vs2022,
                        Optimization.Debug | Optimization.Release | Optimization.Retail,
                        DotNetFramework.v4_8,
                        dotNetOS: 0);
                    editorTarget.SubPlatform = SubPlatformType.Win64;
                    editorTarget.SelectedMode = Mode.Editor;

                    if(Globals.IsUWPEnabled)
                    {
                        return new[] { baseTarget, uwpTarget, editorTarget };
                    }
                    return new[] { baseTarget, editorTarget };
                }
            case Platform.mac:
                {
                    var macOSTarget = new CommonTarget(
                        Platform.mac,
                        DevEnv.xcode4ios,
                        Optimization.Debug | Optimization.Release,
                        DotNetFramework.v4_8,
                        dotNetOS: 0);
                    macOSTarget.SubPlatform = SubPlatformType.macOS;
                    var macEditor = new CommonTarget(
                        Platform.mac,
                        DevEnv.xcode4ios,
                        Optimization.Debug | Optimization.Release,
                        DotNetFramework.v4_8,
                        dotNetOS: 0);
                    macEditor.SelectedMode = Mode.Editor;
                    macEditor.SubPlatform = SubPlatformType.macOS;

                    return new[] { macOSTarget, macEditor };
                }
            default:
                {
                    throw new NotImplementedException("The platform (" + Util.GetExecutingPlatform() + ") is not currently supported!");
                }
        }
    }

    public override string Name
    {
        get
        {
            if (SelectedMode == Mode.Editor)
            {
                var nameParts = new List<string>
                {
                    SelectedMode.ToString(),
                    Optimization.ToString(),
                };
                return string.Join("_", nameParts);
            }
            else
            {
                var nameParts = new List<string>
                {
                    SelectedMode.ToString(),
                    SubPlatform.ToString(),
                    Optimization.ToString(),
                };
                return string.Join("_", nameParts);
            }
        }
    }

    public string DirectoryName
    {
        get
        {
            var dirNameParts = new List<string>()
            {
                SubPlatform.ToString(),
                Optimization.ToString(),
                SelectedMode.ToString(),
            };

            return string.Join("_", dirNameParts);
        }
    }

    public ITarget ToDefaultDotNetOSTarget()
    {
        return ToSpecificDotNetOSTarget(DotNetOS.Default);
    }

    public ITarget ToSpecificDotNetOSTarget(DotNetOS dotNetOS)
    {
        if (DotNetOS == 0 || DotNetOS == dotNetOS)
        {
            return this;
        }

        return Clone(dotNetOS);
    }
}
