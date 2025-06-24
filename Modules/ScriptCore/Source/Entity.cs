using System;
using System.Runtime.CompilerServices;

[Serializable]
public struct EntityID
{
    public ulong Index;
    public ulong Counter;
}

public class Entity
{
    public readonly EntityID EntID;

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
        if (!HasComponent<T>())
        {
            return null;
        }
        
        return new T() { Parent = this };
    }

    public Transform transform
    {
        get { return GetComponent<Transform>(); }
        private set { }
    }


    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    extern static bool Entity_HasComponent(EntityID id, Type type);

}