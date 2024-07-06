using System;

public struct Vector3
{
    public float x, y, z;

    public Vector3( float x, float y, float z )
    {
        this.x = x;
        this.y = y;
        this.z = z;
    }

    public Vector3( Vector3 other )
    {
        x = other.x;
        y = other.y;
        z = other.z;
    }

    public float Length()
    {
        return (float)Math.Sqrt( (double)( x * x + y * y + z * z ) );
    }

    public static Vector3 operator +(Vector3 thiss, Vector3 other)
    {
        return new Vector3(thiss.x + other.x, thiss.y + other.y, thiss.z + other.z);
    }

    public static Vector3 operator -(Vector3 thiss, Vector3 other)
    {
        return new Vector3(thiss.x - other.x, thiss.y - other.y, thiss.z - other.z);
    }

    public static Vector3 operator *(Vector3 thiss, float other)
    {
        return new Vector3(thiss.x * other, thiss.y * other, thiss.z * other);
    }

    public override string ToString()
    {
        return $"{{{Convert.ToString(x)}, {Convert.ToString(y)}, {Convert.ToString(z)}}}";
    }
}