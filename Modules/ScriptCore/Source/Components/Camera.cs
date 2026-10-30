namespace ScriptCore;

public class Camera : Component
{
    public Vector3 ClearColor
    {
        get { unsafe { Vector3 v; Engine._api.Camera_GetClearColor(Entity, &v); return v; } }
        set { unsafe { Engine._api.Camera_SetClearColor(Entity, &value); } }
    }

    /// <summary>
    /// Hides everything behind a world-space plane (the side the normal points away from), as if
    /// the near plane were that surface. A camera looking out of a portal or a mirror uses the exit
    /// surface. Ignored while the camera is in front of the plane.
    /// </summary>
    public void SetClipPlane(Vector3 point, Vector3 normal)
    {
        unsafe { Engine._api.Camera_SetClipPlane(Entity, &point, &normal); }
    }

    public void ClearClipPlane()
    {
        unsafe { Engine._api.Camera_ClearClipPlane(Entity); }
    }

    /// <summary>
    /// Render texture: renders into this .rendertexture asset (a project path such as
    /// "Assets/Textures/Feed.rendertexture"), at the asset's size, instead of to the screen. Materials
    /// show the image by naming the same asset in a texture slot. Empty renders normally.
    /// </summary>
    public string TargetTexture
    {
        get => Entity.GetField<string>("Camera", "TargetTexture", "");
        set => Entity.SetField("Camera", "TargetTexture", value ?? "");
    }

    /// <summary>
    /// CS-5 e: projection. FieldOfView is the vertical angle in degrees (the engine's Zoom field).
    /// </summary>
    public float FieldOfView
    {
        get => Entity.GetField<float>("Camera", "Zoom", 45f);
        set => Entity.SetField("Camera", "Zoom", value);
    }

    public float Near
    {
        get => Entity.GetField<float>("Camera", "Near", 0.1f);
        set => Entity.SetField("Camera", "Near", value);
    }

    public float Far
    {
        get => Entity.GetField<float>("Camera", "Far", 1000f);
        set => Entity.SetField("Camera", "Far", value);
    }

    /// <summary>
    /// Half the view height in metres, for an orthographic camera.
    /// </summary>
    public float OrthographicSize
    {
        get => Entity.GetField<float>("Camera", "OrthographicSize", 5f);
        set => Entity.SetField("Camera", "OrthographicSize", value);
    }

    /// <summary>
    /// The pixels it renders (its output size times its viewport rect).
    /// </summary>
    public Vector2 OutputSize
    {
        get { unsafe { Vector2 size; Engine._api.Camera_GetOutputSize(Entity, &size); return size; } }
    }

    // A ray from the camera through a screen point (pixels from the top left), e.g. the mouse:
    //   camera.ScreenToRay(Input.MousePosition, out var origin, out var direction);
    public void ScreenToRay(Vector2 screen, out Vector3 origin, out Vector3 direction)
    {
        unsafe
        {
            Vector3 o, d;
            Engine._api.Camera_ScreenToRay(Entity, &screen, &o, &d);
            origin = o;
            direction = d;
        }
    }

    /// <summary>
    /// Where a world point lands on screen: x, y in pixels from the top left, z its distance in front
    /// of the camera (negative behind it).
    /// </summary>
    public Vector3 WorldToScreen(Vector3 world)
    {
        unsafe { Vector3 s; Engine._api.Camera_WorldToScreen(Entity, &world, &s); return s; }
    }

    /// <summary>
    /// Render scaling (LOOK-S1, ME-275): the scene renders at OutputSize * RenderScale (0.1..1)
    /// and is stretched back up with Upscale. Dynamic resolution lowers the scale while the GPU is
    /// over budget. The player's Graphics.RenderScale setting multiplies on top.
    /// </summary>
    public float RenderScale
    {
        get => Entity.GetField<float>("Camera", "RenderScale", 1f);
        set => Entity.SetField("Camera", "RenderScale", value);
    }

    public UpscaleFilter Upscale
    {
        get => System.Enum.TryParse(Entity.GetField<string>("Camera", "Upscale", "Bilinear"), out UpscaleFilter filter) ? filter : UpscaleFilter.Bilinear;
        set => Entity.SetField("Camera", "Upscale", value.ToString());
    }

    public bool DynamicResolution
    {
        get => Entity.GetField<bool>("Camera", "DynamicResolution", false);
        set => Entity.SetField("Camera", "DynamicResolution", value);
    }

    /// <summary>
    /// Renders every Nth frame (1 = every frame); the texture keeps its last image in between.
    /// </summary>
    public int UpdateInterval
    {
        get => Entity.GetField<int>("Camera", "UpdateInterval", 1);
        set => Entity.SetField("Camera", "UpdateInterval", value);
    }
}


/// <summary>
/// How a camera's scaled-down image is stretched to its output.
/// </summary>
public enum UpscaleFilter
{
    Bilinear,
    Nearest,
    Pixel,      // whole-number blocks, for pixel art
    Sharp,      // Catmull-Rom plus contrast-adaptive sharpening
}
