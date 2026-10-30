using System.Collections.Generic;

namespace ScriptCore.Kit;

/// <summary>
/// Counts of named items ("Key", "Coin"). Owned by an actor's script (IInventory).
/// </summary>
public sealed class Inventory
{
    private readonly Dictionary<string, int> _items = new();

    /// <summary>Every item and its count.</summary>
    public IReadOnlyDictionary<string, int> Items => _items;
    /// <summary>How many of an item there are.</summary>
    public int Count(string item) => _items.TryGetValue(item, out int count) ? count : 0;
    /// <summary>Whether there are at least amount of an item.</summary>
    public bool Has(string item, int amount = 1) => Count(item) >= amount;

    /// <summary>Adds amount of an item.</summary>
    public void Add(string item, int amount = 1)
    {
        if (string.IsNullOrEmpty(item) || amount <= 0) return;
        _items[item] = Count(item) + amount;
    }

    /// <summary>Removes amount of an item; false (and nothing removed) when there aren't enough.</summary>
    public bool Remove(string item, int amount = 1)
    {
        if (!Has(item, amount)) return false;
        int left = Count(item) - amount;
        if (left > 0) _items[item] = left; else _items.Remove(item);
        return true;
    }

    /// <summary>Removes every item.</summary>
    public void Clear() => _items.Clear();
}
