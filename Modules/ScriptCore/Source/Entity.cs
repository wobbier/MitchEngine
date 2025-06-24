using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

[AttributeUsage(AttributeTargets.Method | AttributeTargets.Class)]
public class UsedImplicitlyAttribute : Attribute { }

[Serializable]
public struct EntityID
{
    public ulong Index;
    public ulong Counter;
}

public class Entity
{
    public readonly EntityID EntID;
    private Dictionary<Type, Component> _componentCache = new Dictionary<Type, Component>();

    protected Entity()
    {
        EntID.Index = 0;
        EntID.Counter = 0;
    }

    internal Entity(EntityID inID)
    {
        EntID = inID;
    }

    public bool HasComponent<T>() where T : Component, new()
    {
        Type type = typeof(T);
        return Entity_HasComponent(EntID, type);
    }

    public T GetComponent<T>() where T: Component, new()
    {
        var type = typeof(T);

        if (_componentCache.TryGetValue(type, out var existing))
            return (T)existing;

        if (!HasComponent<T>())
            return null;

        var comp = new T { Parent = this };
        _componentCache[type] = comp;
        return comp;
    }

    public Transform transform
    {
        get { return GetComponent<Transform>(); }
        private set { }
    }

    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static bool Entity_HasComponent(EntityID id, Type type);
}