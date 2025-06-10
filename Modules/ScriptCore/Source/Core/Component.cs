using System;
using System.Collections.Generic;

public class MEObject
{

}

public abstract class Component : MEObject
{
    public Entity _parent;
    public Entity Parent => _parent;

    public T GetComponent<T>() where T : Component, new()
    {
        if (Parent == null)
        {
            return null;
        }

        return Parent.GetComponent<T>();
    }

    public bool HasComponent<T>() where T : Component
    {
        if (Parent == null)
        {
            return false;
        }

        return Parent.HasComponent<T>();
    }

    public T AddComponent<T>() where T : Component, new()
    {
        if (Parent == null)
        {
            Console.WriteLine("NULL PARENT");
            return null;
        }

        return Parent.AddComponent<T>();
    }
}
