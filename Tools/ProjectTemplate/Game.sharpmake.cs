using System.IO;
using Sharpmake;

[module: Sharpmake.Include("Engine/Engine.sharpmake.cs")]

[Generate]
public class SharpGameProject : BaseGameProject
{
    public SharpGameProject()
        : base()
    {
        Name = "EmptyProject";
    }
}

[Generate]
public class SharpGameSolution : BaseGameSolution
{
    public SharpGameSolution()
        : base()
    {
        Name = "EmptyProject";
    }
}

[Generate]
public class UserSharpmakeProject : SharpmakeProjectBase
{
    public UserSharpmakeProject() : base() { }
}