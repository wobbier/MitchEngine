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
    }
}