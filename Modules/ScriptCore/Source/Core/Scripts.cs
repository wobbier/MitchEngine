using System.Collections.Generic;

namespace ScriptCore;

/// <summary>
/// The live scripts (ME-312), so a game doesn't keep its own static registry of them (which a hot
/// reload empties and every OnStart / OnReload / OnDestroy has to maintain):
///
/// <code>
///    foreach (var pawn in Scripts.All&lt;HeroPawn&gt;())     // no allocation
///        if (pawn.Health &lt;= 0) ...
///    var shooter = Scripts.First&lt;Weapon&gt;(w =&gt; w.Owner == id);
/// </code>
///
/// T is a script class or any interface or base class scripts implement. Scripts are visited in the
/// order they were created (a hot reload keeps it), which is the same on every peer that created
/// them in the same order. Every live script is included, also on inactive entities and on entities
/// destroyed this frame (they leave at the end of the frame, after OnDestroy). Scripts created while
/// iterating are visited by the next loop, not this one.
/// </summary>
public static class Scripts
{
    /// <summary>Every live script that is (or implements) T, in creation order; a struct enumerable, no allocation.</summary>
    public static ScriptSet<T> All<T>() where T : class => default;

    /// <summary>The first live script that is (or implements) T, or null.</summary>
    public static T? First<T>() where T : class
    {
        foreach (var script in All<T>())
        {
            return script;
        }
        return null;
    }

    /// <summary>The first live script that is (or implements) T and matches, or null.</summary>
    public static T? First<T>(System.Predicate<T> match) where T : class
    {
        foreach (var script in All<T>())
        {
            if (match(script))
            {
                return script;
            }
        }
        return null;
    }

    /// <summary>How many live scripts are (or implement) T.</summary>
    public static int Count<T>() where T : class
    {
        int count = 0;
        foreach (var _ in All<T>())
        {
            ++count;
        }
        return count;
    }
}

/// <summary>
/// The live scripts that are T (Scripts.All): foreach over it allocates nothing. CopyTo fills a list
/// to keep, sort or change the world while walking.
/// </summary>
public readonly struct ScriptSet<T> where T : class
{
    /// <summary>For foreach.</summary>
    public Enumerator GetEnumerator() => new Enumerator(ScriptBridge.HandleOrder.Count);

    /// <summary>Clears the list, then adds every match in creation order.</summary>
    public void CopyTo(List<T> list)
    {
        list.Clear();
        foreach (var script in this)
        {
            list.Add(script);
        }
    }

    /// <summary>Walks the scripts that existed when the loop began, in creation order.</summary>
    public struct Enumerator
    {
        private readonly int end;
        private int index;
        private T? current;

        internal Enumerator(int count)
        {
            end = count;
            index = -1;
            current = null;
        }

        /// <summary>The script at the enumerator's position.</summary>
        public T Current => current!;

        /// <summary>Moves to the next live script that is T.</summary>
        public bool MoveNext()
        {
            var order = ScriptBridge.HandleOrder;
            while (++index < end && index < order.Count)
            {
                if (ScriptBridge.TryGetInstance(order[index], out var instance) && instance is T match)
                {
                    current = match;
                    return true;
                }
            }
            current = null;
            return false;
        }
    }
}
