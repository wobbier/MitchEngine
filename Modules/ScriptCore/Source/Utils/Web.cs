using System.Runtime.CompilerServices;

/// <summary>Legacy (Mono): not available under .NET 8.</summary>
public class HTTP
{
    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static string HTTP_DownloadFile(string inString, string inDirectory);
    /// <summary>Legacy (a Mono internal call): it has no implementation under .NET 8 and throws.</summary>
    public static string DownloadFile(string inString, string destination)
    {
        return HTTP_DownloadFile(inString, destination);
    }
}
