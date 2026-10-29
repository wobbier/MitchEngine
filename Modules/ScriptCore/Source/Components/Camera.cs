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

    // Render texture: renders into this .rendertexture asset (a project path such as
    // "Assets/Textures/Feed.rendertexture"), at the asset's size, instead of to the screen. Materials
    // show the image by naming the same asset in a texture slot. Empty renders normally.
    public string TargetTexture
    {
        get => Entity.GetField<string>("Camera", "TargetTexture", "");
        set => Entity.SetField("Camera", "TargetTexture", value ?? "");
    }

    // Renders every Nth frame (1 = every frame); the texture keeps its last image in between.
    public int UpdateInterval
    {
        get => Entity.GetField<int>("Camera", "UpdateInterval", 1);
        set => Entity.SetField("Camera", "UpdateInterval", value);
    }
}
