using System;
using System.Collections.Generic;

public class MEObject
{

}

public abstract class Component : MEObject
{
    public Entity Parent { get; internal set; }

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
}
