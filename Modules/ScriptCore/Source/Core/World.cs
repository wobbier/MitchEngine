using System.Runtime.CompilerServices;

public class World
{
    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    public static extern EntityID World_GetTransformByName(string inString);
    public static Transform GetTransformByName(string inName)
    {
        return new Entity(World_GetTransformByName(inName)).GetComponent<Transform>();
    }
}
