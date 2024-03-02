using Sharpmake;

[Generate]
public class ShaderEditor : BaseTool
{
    public ShaderEditor()
        : base()
    {
        Name = "ShaderEditor";
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
    }
}