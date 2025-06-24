using System.Runtime.CompilerServices;

public class HTTP
{
    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static string HTTP_DownloadFile(string inString, string inDirectory);
    public static string DownloadFile(string inString, string destination)
    {
        return HTTP_DownloadFile(inString, destination);
    }
}
