using System.Collections.Generic;

namespace ScriptCore.Kit;

/// <summary>
/// Counts of named items ("Key", "Coin"). Owned by an actor's script (IInventory).
/// </summary>
public sealed class Inventory
{
    private readonly Dictionary<string, int> _items = new();

    public IReadOnlyDictionary<string, int> Items => _items;
    public int Count(string item) => _items.TryGetValue(item, out int count) ? count : 0;
    public bool Has(string item, int amount = 1) => Count(item) >= amount;

    public void Add(string item, int amount = 1)
    {
        if (string.IsNullOrEmpty(item) || amount <= 0) return;
        _items[item] = Count(item) + amount;
    }

    public bool Remove(string item, int amount = 1)
    {
        if (!Has(item, amount)) return false;
        int left = Count(item) - amount;
        if (left > 0) _items[item] = left; else _items.Remove(item);
        return true;
    }

    public void Clear() => _items.Clear();
}
