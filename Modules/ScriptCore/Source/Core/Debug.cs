using System.Runtime.CompilerServices;

public class Debug
{
    [MethodImplAttribute( MethodImplOptions.InternalCall )]
    public static extern void Log( string inString );

    [MethodImplAttribute( MethodImplOptions.InternalCall )]
    public static extern void Error( string inString );

    public static void Log( object inObject )
    {
        Log( inObject.ToString() );
    }

    public static void Error( object inObject )
    {
        Error( inObject.ToString() );
    }
}
