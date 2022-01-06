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
        var baseTarget = new CommonTarget(
            Platform.win64,
            DevEnv.vs2019,
            Optimization.Debug | Optimization.Release,
            DotNetFramework.v4_8,
            dotNetOS: 0);

        var editorTarget = new CommonTarget(
            Platform.win64,
            DevEnv.vs2019,
            Optimization.Debug | Optimization.Release,
            DotNetFramework.v4_8,
            dotNetOS: 0);
        editorTarget.SelectedMode = Mode.Editor;

        return new[] { baseTarget, editorTarget };
    }

    public override string Name
    {
        get
        {
            var nameParts = new List<string>
            {
                Optimization.ToString(),
                SelectedMode.ToString(),
            };
            return string.Join(" ", nameParts);
        }
    }

    public string DirectoryName
    {
        get
        {
            var dirNameParts = new List<string>()
            {
                Platform.ToString(),
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
            return this;

        return Clone(dotNetOS);
    }
}