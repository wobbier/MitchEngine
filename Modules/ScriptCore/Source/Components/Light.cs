namespace ScriptCore;

/// <summary>
/// The entity's Light (CS-5 e): its reflected fields as properties. Linear color, intensity in the
/// engine's units, range in metres, cone angles in degrees (spot lights).
/// </summary>
public class Light : Component
{
    /// <summary>The light's linear RGB color.</summary>
    public Vector3 Color
    {
        get => Entity.GetField<Vector3>("Light", "Color", new Vector3(1f, 1f, 1f));
        set => Entity.SetField("Light", "Color", value);
    }

    /// <summary>The intensity, in the engine's units.</summary>
    public float Intensity
    {
        get => Entity.GetField<float>("Light", "Intensity", 1f);
        set => Entity.SetField("Light", "Intensity", value);
    }

    /// <summary>How far a local light reaches, in metres.</summary>
    public float Range
    {
        get => Entity.GetField<float>("Light", "Range", 10f);
        set => Entity.SetField("Light", "Range", value);
    }

    /// <summary>Spot lights: the full-intensity half-angle, degrees.</summary>
    public float InnerConeAngle
    {
        get => Entity.GetField<float>("Light", "InnerConeAngle", 20f);
        set => Entity.SetField("Light", "InnerConeAngle", value);
    }

    /// <summary>Spot lights: the outer half-angle where the beam fades out, degrees.</summary>
    public float OuterConeAngle
    {
        get => Entity.GetField<float>("Light", "OuterConeAngle", 30f);
        set => Entity.SetField("Light", "OuterConeAngle", value);
    }
}
