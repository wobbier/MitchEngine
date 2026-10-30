using System.Runtime.InteropServices;

namespace ScriptCore;

/// <summary>
/// Matches the engine's ScriptRaycastHit.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct RaycastHit
{
    /// <summary>The entity whose collider was hit.</summary>
    public Entity Entity;
    /// <summary>Where the ray hit, world space.</summary>
    public Vector3 Point;
    /// <summary>The surface normal there.</summary>
    public Vector3 Normal;
    /// <summary>How far along the ray, in metres.</summary>
    public float Distance;
}

/// <summary>
/// A contact reported to OnCollisionEnter / OnCollisionExit.
/// </summary>
public struct Collision
{
    /// <summary>The entity whose collider was touched</summary>
    public Entity Other;
    /// <summary>Contact point (enter events of 3D contacts)</summary>
    public Vector3 Point;
    /// <summary>From this entity towards the other</summary>
    public Vector3 Normal;
}

/// <summary>How Rigidbody.AddForce applies a vector.</summary>
public enum ForceMode
{
    /// <summary>Continuous, mass dependent</summary>
    Force = 0,
    /// <summary>Instant, mass dependent</summary>
    Impulse,
    /// <summary>Continuous, ignores mass</summary>
    Acceleration,
    /// <summary>Instant, ignores mass</summary>
    VelocityChange,
}

/// <summary>Queries against the physics world: raycasts, overlaps, portal passages.</summary>
public static unsafe class Physics
{
    // Opens only this rectangular passage in the host collider for fitting characters/bodies.
    // The rest of the wall, other colliders, and raycasts stay solid. Update after moving a portal;
    // clear when its linked endpoint is unavailable or the portal is destroyed.
    /// <summary>Opens only this rectangular passage in the host collider for fitting characters and bodies; the rest of the wall, other colliders and raycasts stay solid. Update it after moving a portal.</summary>
    public static bool SetPortalPassage(Entity portal, Entity host, Vector3 center, Vector3 right,
        Vector3 up, Vector2 halfSize, float depth = 2.5f)
        => Engine._api.Physics_SetPortalPassage(portal, host, &center, &right, &up, halfSize.x, halfSize.y, depth) != 0;

    /// <summary>Closes a portal's passage (its linked end is gone, or the portal is destroyed).</summary>
    public static void ClearPortalPassage(Entity portal) => Engine._api.Physics_ClearPortalPassage(portal);

    /// <summary>
    /// A mask of the given layers (0..31).
    /// </summary>
    public static int LayerMask(params int[] layers)
    {
        int mask = 0;
        foreach (int layer in layers) mask |= 1 << (layer & 31);
        return mask;
    }

    /// <summary>
    /// The first 3D collider along the ray (direction need not be normalized).
    /// Every entity with a collider touching the sphere (triggers excluded), up to maxResults.
    /// </summary>
    public static Entity[] OverlapSphere(Vector3 center, float radius, int maxResults = 1024)
    {
        var buffer = new Entity[maxResults];
        int total;
        fixed (Entity* p = buffer) total = Engine._api.Physics_OverlapSphere(&center, radius, p, maxResults);
        int count = System.Math.Min(total, maxResults);
        if (count == maxResults)
        {
            return buffer;
        }
        var result = new Entity[count];
        System.Array.Copy(buffer, result, count);
        return result;
    }

    /// <summary>
    /// CS-5 f: layerMask bit n = layer n (Layers.Mask(...)); -1 = every layer.
    /// </summary>
    public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, int layerMask, out RaycastHit hit)
    {
        RaycastHit h;
        bool found = Engine._api.Physics_RaycastMasked(&origin, &direction, maxDistance, layerMask, &h) != 0;
        hit = h;
        return found;
    }

    /// <summary>
    /// Every hit along the ray, nearest first.
    /// </summary>
    public static RaycastHit[] RaycastAll(Vector3 origin, Vector3 direction, float maxDistance, int layerMask = -1, int maxResults = 256)
    {
        var buffer = new RaycastHit[maxResults];
        int total;
        fixed (RaycastHit* p = buffer) total = Engine._api.Physics_RaycastAll(&origin, &direction, maxDistance, layerMask, p, maxResults);
        System.Array.Resize(ref buffer, System.Math.Clamp(total, 0, maxResults));
        return buffer;
    }

    /// <summary>
    /// A sphere swept along the ray: the first collider it touches.
    /// </summary>
    public static bool SphereCast(Vector3 origin, float radius, Vector3 direction, float maxDistance, out RaycastHit hit, int layerMask = -1)
    {
        RaycastHit h;
        bool found = Engine._api.Physics_SphereCast(&origin, radius, &direction, maxDistance, layerMask, &h) != 0;
        hit = h;
        return found;
    }

    /// <summary>
    /// Every entity with a collider touching the box (rotated by Euler degrees).
    /// </summary>
    public static Entity[] OverlapBox(Vector3 center, Vector3 halfExtents, Vector3 eulerDegrees, int layerMask = -1)
    {
        // Copies: a lambda can't take the parameters' addresses.
        Vector3 c = center, h = halfExtents, e = eulerDegrees;
        return Query.Fill((buffer, max) =>
        {
            Vector3 cc = c, hh = h, ee = e;
            fixed (Entity* b = buffer) return Engine._api.Physics_OverlapBox(&cc, &hh, &ee, layerMask, b, max);
        });
    }

    /// <summary>The first collider along the ray within maxDistance metres; false on a miss.</summary>
    public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, out RaycastHit hit)
    {
        RaycastHit result;
        bool found = Engine._api.Physics_Raycast(&origin, &direction, maxDistance, &result) != 0;
        hit = result;
        return found;
    }
}
