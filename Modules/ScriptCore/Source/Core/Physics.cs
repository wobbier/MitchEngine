using System.Runtime.InteropServices;

namespace ScriptCore;

// Matches the engine's ScriptRaycastHit.
[StructLayout(LayoutKind.Sequential)]
public struct RaycastHit
{
    public Entity Entity;
    public Vector3 Point;
    public Vector3 Normal;
    public float Distance;
}

// A contact reported to OnCollisionEnter / OnCollisionExit.
public struct Collision
{
    public Entity Other;        // the entity whose collider was touched
    public Vector3 Point;       // contact point (enter events of 3D contacts)
    public Vector3 Normal;      // from this entity towards the other
}

public enum ForceMode
{
    Force = 0,          // continuous, mass dependent
    Impulse,            // instant, mass dependent
    Acceleration,       // continuous, ignores mass
    VelocityChange,     // instant, ignores mass
}

public static unsafe class Physics
{
    // The first 3D collider along the ray (direction need not be normalized).
    public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, out RaycastHit hit)
    {
        RaycastHit result;
        bool found = Engine._api.Physics_Raycast(&origin, &direction, maxDistance, &result) != 0;
        hit = result;
        return found;
    }
}
