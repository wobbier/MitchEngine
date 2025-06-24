public class MEObject
{

}

public abstract class Component : Entity
{
    public Entity Parent { get; internal set; }
}
