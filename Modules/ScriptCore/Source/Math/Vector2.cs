
public struct Vector2
{
    public float x, y;

    public Vector2(float x, float y)
    {
        this.x = x;
        this.y = y;
    }

    public Vector2(Vector2 other)
    {
        x = other.x;
        y = other.y;
    }

    public Vector2(Vector3 other)
    {
        x = other.x;
        y = other.y;
    }
}