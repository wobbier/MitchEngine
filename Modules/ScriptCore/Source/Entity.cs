using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.CompilerServices;

[AttributeUsage(AttributeTargets.Method | AttributeTargets.Class)]
public class UsedImplicitlyAttribute : Attribute { }

[Serializable]
public struct EntityID
{
    public ulong Index;
    public ulong Counter;
}

// Everything in your world is an Entity
public class Entity : MEObject
{
    public EntityID EntID { get; protected set; } // No readonly! Use private set if you want more control

    private Dictionary<Type, Component> _componentCache = new Dictionary<Type, Component>();

    protected Entity()
    {
        Console.WriteLine("Created a new entity");
        EntID = new EntityID { Index = 0, Counter = 0 };
    }

    internal Entity(EntityID inID)
    {
        Console.WriteLine("Created a new entity with ID");
        EntID = inID;
    }

    public bool HasComponent<T>() where T : Component
    {
        Type type = typeof(T);

        if (_componentCache.TryGetValue(type, out var existing))
            return true;

        return Entity_HasComponent(EntID, type);
    }

    public T GetComponent<T>() where T : Component, new()
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

    public Transform transform => GetComponent<Transform>();

    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static bool Entity_HasComponent(EntityID id, Type type);
}
