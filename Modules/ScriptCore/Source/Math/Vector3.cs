
public struct Vector3
{
    public float x, y, z;

    public Vector3(float x, float y, float z)
    {
        this.x = x;
        this.y = y;
        this.z = z;
    }

    public Vector3(Vector3 other)
    {
        x = other.x;
        y = other.y;
        z = other.z;
    }
}