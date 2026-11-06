using System;
using System.Collections.Generic;
using System.Reflection;
using ScriptCore;
using Xunit;

// ME-312: Scripts.All<T>() walks the live scripts in creation order, by class or interface, without
// allocating, and only the scripts that existed when the loop began.
public interface IScriptsAllTagged { int Tag { get; } }
public class ScriptsAllPawn : Script, IScriptsAllTagged { public int Tag { get; set; } }
public class ScriptsAllBot : ScriptsAllPawn { }
public class ScriptsAllOther : Script { }

[Collection("ScriptRegistry")]
public class ScriptsAllTests
{
    private static Dictionary<int, IGameScript> Instances =>
        (Dictionary<int, IGameScript>)typeof(ScriptBridge).GetField("Instances", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null)!;
    private static List<int> Order =>
        (List<int>)typeof(ScriptBridge).GetField("_handleOrder", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null)!;

    private static void Add(int handle, IGameScript script)
    {
        Instances[handle] = script;
        Order.Add(handle);
    }

    [Fact]
    public void All_visits_matching_scripts_in_creation_order_without_allocating()
    {
        Assert.Empty(Instances);
        try
        {
            // Handles out of numeric order: creation order is the list's, not the handles'.
            Add(50, new ScriptsAllPawn { Tag = 1 });
            Add(10, new ScriptsAllOther());
            Add(30, new ScriptsAllBot { Tag = 2 });
            Add(20, new ScriptsAllPawn { Tag = 3 });

            var tags = new List<int>();
            foreach (var pawn in Scripts.All<ScriptsAllPawn>())
            {
                tags.Add(pawn.Tag);
            }
            Assert.Equal(new[] { 1, 2, 3 }, tags);
            Assert.Equal(1, Scripts.Count<ScriptsAllBot>());
            Assert.Equal(3, Scripts.Count<IScriptsAllTagged>());
            Assert.Equal(4, Scripts.Count<Script>());
            Assert.Equal(2, Scripts.First<IScriptsAllTagged>(t => t.Tag > 1)!.Tag);
            Assert.Null(Scripts.First<ScriptsAllPawn>(p => p.Tag > 5));

            var list = new List<IScriptsAllTagged> { new ScriptsAllPawn() };
            Scripts.All<IScriptsAllTagged>().CopyTo(list);
            Assert.Equal(3, list.Count);

            // Warm up (JIT, generic instantiation), then measure a loop.
            int sum = 0;
            foreach (var tagged in Scripts.All<IScriptsAllTagged>()) sum += tagged.Tag;
            long before = GC.GetAllocatedBytesForCurrentThread();
            for (int i = 0; i < 100; ++i)
            {
                foreach (var tagged in Scripts.All<IScriptsAllTagged>()) sum += tagged.Tag;
            }
            Assert.Equal(0, GC.GetAllocatedBytesForCurrentThread() - before);
            Assert.Equal(6 * 101, sum);

            // A script created inside the loop waits for the next one.
            int visited = 0;
            foreach (var pawn in Scripts.All<ScriptsAllPawn>())
            {
                if (visited++ == 0) Add(60, new ScriptsAllPawn { Tag = 4 });
            }
            Assert.Equal(3, visited);
            Assert.Equal(4, Scripts.Count<ScriptsAllPawn>());

            // A destroyed script (its handle gone) is skipped even if the order still names it.
            Instances.Remove(30);
            Assert.Equal(3, Scripts.Count<ScriptsAllPawn>());
        }
        finally
        {
            Instances.Clear();
            Order.Clear();
        }
    }
}
