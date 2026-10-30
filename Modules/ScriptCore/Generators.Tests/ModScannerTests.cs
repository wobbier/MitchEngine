using System.IO;
using System.Linq;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using ScriptCore;
using Xunit;

// MOD-7: the load-time scan labels a mod assembly that reaches past the script API.
public class ModScannerTests
{
    private static byte[] Compile(string source)
    {
        var trusted = (string)System.AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")!;
        var references = trusted.Split(Path.PathSeparator).Select(p => MetadataReference.CreateFromFile(p)).ToList();
        references.Add(MetadataReference.CreateFromFile(typeof(Script).Assembly.Location));
        var compilation = CSharpCompilation.Create("ScannedMod", new[] { CSharpSyntaxTree.ParseText(source) }, references,
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, allowUnsafe: true));
        using var stream = new MemoryStream();
        var emitted = compilation.Emit(stream);
        Assert.True(emitted.Success, string.Join("\n", emitted.Diagnostics));
        return stream.ToArray();
    }


    [Fact]
    public void A_script_only_mod_is_verified()
    {
        var result = ModScanner.Scan(Compile(@"using ScriptCore; using System.Collections.Generic;
public class Spinner : Script
{
    private readonly List<float> history = new();
    public override void OnUpdate(float dt) { history.Add(dt); transform.Rotate(new Vector3(0, 90 * dt, 0)); Debug.Log(System.IO.Path.GetFileName(""a/b.txt"")); }
}"));
        Assert.True(result.Verified, string.Join("; ", result.Issues));
    }


    [Fact]
    public void Starting_a_process_is_flagged()
    {
        var result = ModScanner.Scan(Compile(@"using ScriptCore;
public class Launcher : Script
{
    public override void OnStart() { System.Diagnostics.Process.Start(""calc""); }
}"));
        Assert.False(result.Verified);
        Assert.Contains(result.Issues, i => i.Contains("System.Diagnostics.Process") && i.Contains("starts programs"));
    }


    [Fact]
    public void Native_methods_emit_and_direct_file_access_are_flagged()
    {
        var result = ModScanner.Scan(Compile(@"using System.Runtime.InteropServices;
public static class Native
{
    [DllImport(""libc"")] public static extern int getpid();
    public static void Write() { System.IO.File.WriteAllText(""/tmp/x"", ""y""); }
    public static object Make() { return new System.Reflection.Emit.DynamicMethod(""m"", null, null); }
}"));
        Assert.Contains(result.Issues, i => i.Contains("Native.getpid") && i.Contains("P/Invoke"));
        Assert.Contains(result.Issues, i => i.Contains("System.IO.File ("));
        Assert.Contains(result.Issues, i => i.Contains("System.Reflection.Emit.DynamicMethod"));
    }


    [Fact]
    public void Something_that_isnt_an_assembly_is_unverified()
    {
        Assert.False(ModScanner.Scan(new byte[] { 1, 2, 3, 4 }).Verified);
    }
}
