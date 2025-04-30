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

    // #TODO: This shouldn't be here, investigate removing it...
    public EntityID EntID { get; protected set; } // No readonly! Use private set if you want more control

    protected Component()
    {
        Console.WriteLine("Created a new entity from component");
        EntID = new EntityID { Index = 0, Counter = 0 };
    }

    internal Component(EntityID inID)
    {
        Console.WriteLine("Created a new entity with ID from component");
        EntID = inID;
    }
}
