namespace ScriptCore;

public class Camera : Component
{
    public Vector3 ClearColor
    {
        get { unsafe { Vector3 v; Engine._api.Camera_GetClearColor(Entity, &v); return v; } }
        set { unsafe { Engine._api.Camera_SetClearColor(Entity, &value); } }
    }

    // Hides everything behind a world-space plane (the side the normal points away from), as if
    // the near plane were that surface. A camera looking out of a portal or a mirror uses the exit
    // surface. Ignored while the camera is in front of the plane.
    public void SetClipPlane(Vector3 point, Vector3 normal)
    {
        unsafe { Engine._api.Camera_SetClipPlane(Entity, &point, &normal); }
    }

    public void ClearClipPlane()
    {
        unsafe { Engine._api.Camera_ClearClipPlane(Entity); }
    }
}
