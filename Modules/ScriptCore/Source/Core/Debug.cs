using System.Runtime.CompilerServices;

public class Debug
{
    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    public static extern void Log(string key);

    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    public static extern void Error(string key);
}
