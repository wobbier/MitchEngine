using Sharpmake;

[Generate]
public class MitchHubProject : BaseTool
{
    public MitchHubProject()
        : base()
    {
        Name = "HUB";
        SourceRootPath = @"Source";
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);

        if (target.SubPlatform == CommonTarget.SubPlatformType.linux)
        {
            conf.AdditionalCompilerOptions.Add("`pkg-config --cflags gtk+-3.0`");
            conf.AdditionalLinkerOptions.Add("`pkg-config --libs gtk+-3.0`");
        }
    }
}
