using System.Runtime.CompilerServices;

public class World
{
    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    public static extern Entity World_CreateEntity(string inString);
    public static Entity CreateEntity(string inName)
    {
        return World_CreateEntity(inName);
    }

    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    public static extern Entity World_GetTransformByName(string inString);
    public static Transform GetTransformByName(string inName)
    {
        return World_GetTransformByName(inName).GetComponent<Transform>();
    }
}
