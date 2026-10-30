using System;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;

namespace ScriptCore;

// MOD-7: what a mod's assembly reaches beyond the script API, read from its metadata before it
// loads (nothing runs). Mods are trusted (they load either way); this only labels them, so a
// Workshop build or the mod manager can say "unverified" next to one that declares native
// (P/Invoke) methods or uses the APIs below. A heuristic, not a sandbox: reflection over strings
// can still reach anything, which is why the label is "verified", not "safe".
public sealed class ModScanResult
{
    public bool Verified => Issues.Count == 0;
    public List<string> Issues { get; } = new();
}

public static class ModScanner
{
    // Types a mod shouldn't need (whole namespaces end with '*'), and why.
    private static readonly (string Name, string Why)[] Denied =
    {
        ("System.Diagnostics.Process", "starts programs"),
        ("System.Diagnostics.ProcessStartInfo", "starts programs"),
        ("System.Reflection.Emit.*", "generates code at run time"),
        ("System.Runtime.InteropServices.NativeLibrary", "loads native libraries"),
        ("System.Runtime.InteropServices.Marshal", "reads and writes raw memory"),
        ("System.IO.File", "reads or writes files directly (use the engine's assets and user://)"),
        ("System.IO.FileInfo", "reads or writes files directly (use the engine's assets and user://)"),
        ("System.IO.FileStream", "reads or writes files directly (use the engine's assets and user://)"),
        ("System.IO.Directory", "reads or writes files directly (use the engine's assets and user://)"),
        ("System.IO.DirectoryInfo", "reads or writes files directly (use the engine's assets and user://)"),
        ("System.Net.Sockets.*", "opens network connections"),
        ("System.Net.Http.*", "opens network connections"),
        ("System.Net.WebClient", "opens network connections"),
    };

    public static ModScanResult Scan(byte[] image)
    {
        var result = new ModScanResult();
        try
        {
            using var pe = new PEReader(System.Collections.Immutable.ImmutableArray.Create(image));
            if (!pe.HasMetadata)
            {
                result.Issues.Add("isn't a .NET assembly");
                return result;
            }
            var metadata = pe.GetMetadataReader();
            var seen = new HashSet<string>();
            foreach (var handle in metadata.TypeReferences)
            {
                var type = metadata.GetTypeReference(handle);
                var space = metadata.GetString(type.Namespace);
                var name = metadata.GetString(type.Name);
                var full = space.Length > 0 ? space + "." + name : name;
                foreach (var (denied, why) in Denied)
                {
                    bool match = denied.EndsWith(".*")
                        ? space == denied[..^2] || space.StartsWith(denied[..^1], StringComparison.Ordinal)
                        : full == denied;
                    if (match && seen.Add(full))
                    {
                        result.Issues.Add($"uses {full} ({why})");
                    }
                }
            }
            foreach (var handle in metadata.MethodDefinitions)
            {
                var method = metadata.GetMethodDefinition(handle);
                if ((method.Attributes & MethodAttributes.PinvokeImpl) != 0)
                {
                    var owner = metadata.GetTypeDefinition(method.GetDeclaringType());
                    result.Issues.Add($"declares native method {metadata.GetString(owner.Name)}.{metadata.GetString(method.Name)} (P/Invoke)");
                }
            }
        }
        catch (Exception ex) when (ex is BadImageFormatException || ex is InvalidOperationException)
        {
            result.Issues.Add("couldn't be read (" + ex.Message + ")");
        }
        return result;
    }
}
