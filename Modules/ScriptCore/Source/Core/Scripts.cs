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
/// them in the same order. Scripts created while iterating are visited by the next loop, not this one.
///
/// "Live" means created, not started, so it isn't quite a registry filled in OnStart:
/// - a script is listed from its creation, before its OnStart: one instantiated this frame (it starts
///   before the next update) or one on an entity inactive since the scene loaded (it starts when the
///   entity activates) still has whatever OnStart sets at its default (null references);
/// - scripts on entities destroyed this frame stay listed until the end of the frame;
/// - deactivating a started script's entity destroys the script (OnDestroy): it's gone from All, and
///   reactivating creates a new one from its saved fields, last in the order.
/// Guard on what OnStart sets (or set it in field initializers) when a loop can meet a fresh script.
///
/// Per world (ME-343): a script lists the scripts of its own world, so a [ReplayWorld] script in a
/// killcam's replay world sees that world's copies and the live world's scripts never see them.
/// </summary>
public static class Scripts
{
    /// <summary>Every live script that is (or implements) T in the caller's world, in creation order; a struct enumerable, no allocation.</summary>
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
    /// <summary>For foreach: the scripts of the world the loop runs in (Engine.WorldId).</summary>
    public Enumerator GetEnumerator() => new Enumerator(ScriptBridge.HandleOrder.Count, Engine.WorldId);

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
        private readonly int world;
        private int index;
        private T? current;

        internal Enumerator(int count, int world)
        {
            end = count;
            this.world = world;
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
                if (ScriptBridge.TryGetInstance(order[index], out var instance) && instance is T match && ScriptBridge.InWorld(order[index], world))
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
