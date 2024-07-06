using System;

public struct Vector2
{
    public float x, y;

    public Vector2( float x, float y )
    {
        this.x = x;
        this.y = y;
    }

    public Vector2( Vector2 other )
    {
        x = other.x;
        y = other.y;
    }

    public Vector2( Vector3 other )
    {
        x = other.x;
        y = other.y;
    }

    public float Length()
    {
        return (float)Math.Sqrt( (double)( x * x + y * y ) );
    }

    public static Vector2 operator +( Vector2 thiss, Vector2 other )
    {
        return new Vector2( thiss.x + other.x, thiss.y + other.y );
    }

    public static Vector2 operator -( Vector2 thiss, Vector2 other )
    {
        return new Vector2( thiss.x - other.x, thiss.y - other.y );
    }

    public static Vector2 operator *( Vector2 thiss, float other )
    {
        return new Vector2( thiss.x * other, thiss.y * other );
    }

    public override string ToString()
    {
        return $"{{ {Convert.ToString( x )}, {Convert.ToString( y )} }}";
    }
}