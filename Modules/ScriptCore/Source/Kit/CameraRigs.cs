using System;
using System.Collections.Generic;

namespace ScriptCore.Kit;

// GAME-1 camera rigs. Each VirtualCamera is an entity that works out where a camera would be
// (following, orbiting, looking at something); the CameraBrain on the real camera shows the
// highest-Priority one and blends when that changes. Raise a rig's Priority (or activate its
// entity) to cut or blend to it; CameraImpulse.Shake adds shake on top.

/// <summary>How a VirtualCamera places itself.</summary>
public enum CameraRigMode
{
    /// <summary>Stays where its entity is (LookAt optional)</summary>
    Fixed,
    /// <summary>Target's position + Offset, looking at the target</summary>
    Follow,
    /// <summary>Third person: Look turns it around Target; pulls in when walls are between</summary>
    Orbit,
    /// <summary>Tracks Target's X/Y with a dead zone, Distance back along -Z, no turning</summary>
    Platformer2D,
}

/// <summary>Where a camera is and where it looks.</summary>
/// <param name="Position">World position.</param>
/// <param name="Forward">View direction.</param>
/// <param name="Up">Up direction.</param>
/// <param name="FieldOfView">Degrees (0: leave the camera's).</param>
public readonly record struct CameraPose(Vector3 Position, Vector3 Forward, Vector3 Up, float FieldOfView);

/// <summary>A camera rig: where the camera should be. The CameraBrain shows the live rig with the highest Priority, blending between them.</summary>
public class VirtualCamera : Script
{
    /// <summary>Higher wins; ties go to the rig that started last.</summary>
    public int Priority = 10;
    /// <summary>How it places itself.</summary>
    public CameraRigMode Mode = CameraRigMode.Orbit;
    /// <summary>The entity it follows or orbits (by name).</summary>
    public string Target = "";
    /// <summary>Fixed / Follow: an entity to keep looking at (Follow defaults to Target)</summary>
    [Tooltip("Fixed / Follow: an entity to keep looking at (Follow defaults to Target)")]
    public string LookAt = "";
    /// <summary>Follow: from the target, in world units.</summary>
    [Tooltip("Follow: from the target, in world units")]
    public Vector3 Offset = new Vector3(0f, 3f, -6f);
    /// <summary>Orbit and Platformer2D: how far back from the target, in metres.</summary>
    public float Distance = 7.0f;
    /// <summary>Orbit: the pivot's height above the target.</summary>
    public float Height = 1.5f;
    /// <summary>Orbit: the starting yaw, degrees.</summary>
    public float Yaw = 0.0f;
    /// <summary>Orbit: the starting pitch, degrees (positive looks down).</summary>
    public float Pitch = 20.0f;
    /// <summary>Orbit: degrees per unit of the Look action (0 = no player control)</summary>
    [Tooltip("Orbit: degrees per unit of the Look action (0 = no player control)")]
    public float Sensitivity = 0.15f;
    /// <summary>Orbit: the lowest pitch, degrees.</summary>
    public float MinPitch = -10f;
    /// <summary>Orbit: the highest pitch, degrees.</summary>
    public float MaxPitch = 70f;
    /// <summary>Seconds for the position to close half the gap (0 = locked on)</summary>
    [Tooltip("Seconds for the position to close half the gap (0 = locked on)")]
    public float FollowHalfLife = 0.05f;
    /// <summary>Platformer2D: how far the target moves before the camera does.</summary>
    [Tooltip("Platformer2D: how far the target moves before the camera does")]
    public Vector3 DeadZone = new Vector3(1.0f, 0.75f, 0f);
    /// <summary>Orbit: pull in when a wall is between the camera and the target.</summary>
    public bool AvoidWalls = true;
    /// <summary>Keep the camera inside ConfineMin..ConfineMax.</summary>
    [Tooltip("Keep the camera inside ConfineMin..ConfineMax")]
    public bool Confine = false;
    /// <summary>The confinement box's minimum corner.</summary>
    public Vector3 ConfineMin = new Vector3(-50f, 0f, -50f);
    /// <summary>The confinement box's maximum corner.</summary>
    public Vector3 ConfineMax = new Vector3(50f, 30f, 50f);
    /// <summary>Degrees; 0 leaves the camera's field of view alone.</summary>
    [Tooltip("Degrees; 0 leaves the camera's field of view alone")]
    public float FieldOfView = 0f;
    /// <summary>Seconds to blend in when this rig takes over (0 = cut)</summary>
    [Tooltip("Seconds to blend in when this rig takes over (0 = cut)")]
    public float BlendSeconds = 1.0f;

    /// <summary>Where the rig wants the camera this frame.</summary>
    public CameraPose Pose { get; private set; }
    private Entity _target;
    private Entity _lookAt;
    private bool _placed;

    /// <inheritdoc/>
    public override void OnStart()
    {
        _target = Target.Length > 0 ? World.Find(Target) : default;
        _lookAt = LookAt.Length > 0 ? World.Find(LookAt) : _target;
        Pose = new CameraPose(transform.WorldPosition, transform.Forward, Vector3.Up, FieldOfView);
    }

    /// <inheritdoc/>
    public override void OnReload() => OnStart();

    /// <summary>
    /// Runs before the brain (which is late), so the brain sees this frame's pose.
    /// </summary>
    public override void OnUpdate(float deltaTime)
    {
        Vector3 position = transform.WorldPosition;
        Vector3? lookAt = _lookAt ? _lookAt.Transform.WorldPosition : null;
        switch (Mode)
        {
            case CameraRigMode.Follow:
                if (_target)
                {
                    position = Damp(position, _target.Transform.WorldPosition + Offset, deltaTime);
                }
                break;
            case CameraRigMode.Orbit:
                if (_target)
                {
                    if (Sensitivity != 0f)
                    {
                        Vector2 look = Input.GetActionVector2("Look");
                        Yaw += look.x * Sensitivity;
                        Pitch = Mathf.Clamp(Pitch - look.y * Sensitivity, MinPitch, MaxPitch);
                    }
                    Vector3 forward = CameraRigMath.OrbitForward(Yaw, Pitch);
                    Vector3 pivot = _target.Transform.WorldPosition + Vector3.Up * Height;
                    float distance = Distance;
                    if (AvoidWalls && Physics.Raycast(pivot, -forward, Distance, out var hit) && hit.Entity != _target)
                    {
                        distance = Mathf.Max(hit.Distance - 0.3f, 0.5f);
                    }
                    position = Damp(position, pivot - forward * distance, deltaTime);
                    lookAt = pivot;
                }
                break;
            case CameraRigMode.Platformer2D:
                if (_target)
                {
                    var t = _target.Transform.WorldPosition + Offset;
                    var desired = new Vector3(CameraRigMath.DeadZone(position.x, t.x, DeadZone.x), CameraRigMath.DeadZone(position.y, t.y, DeadZone.y), t.z - Distance);
                    position = Damp(position, desired, deltaTime);
                    lookAt = null;
                }
                break;
        }
        if (Confine)
        {
            position = new Vector3(Mathf.Clamp(position.x, ConfineMin.x, ConfineMax.x), Mathf.Clamp(position.y, ConfineMin.y, ConfineMax.y), Mathf.Clamp(position.z, ConfineMin.z, ConfineMax.z));
        }
        _placed = true;
        transform.WorldPosition = position;
        if (lookAt.HasValue && (lookAt.Value - position).LengthSquared() > 1e-8f)
        {
            transform.LookAt(lookAt.Value);
        }
        Pose = new CameraPose(position, transform.Forward, transform.Up, FieldOfView);
    }

    private Vector3 Damp(Vector3 from, Vector3 to, float deltaTime)
    {
        // The first frame lands on the spot instead of sweeping in from wherever the entity was.
        return !_placed || FollowHalfLife <= 0f ? to : Mathf.Damp(from, to, FollowHalfLife, deltaTime);
    }
}

/// <summary>
/// Goes on the entity with the Camera. Shows the live VirtualCamera with the highest Priority (ties:
/// the one that started last), blending over the new one's BlendSeconds when that changes.
/// </summary>
[ExecutionOrder(1000)]
public class CameraBrain : Script
{
    /// <summary>Blend used when a rig's own BlendSeconds is negative.</summary>
    [Tooltip("Blend used when a rig's own BlendSeconds is negative")]
    public float DefaultBlend = 1.0f;

    /// <summary>The rig shown now.</summary>
    public VirtualCamera? Active { get; private set; }
    /// <summary>Whether it's blending to a new rig.</summary>
    public bool IsBlending => _blendTime < _blendLength;
    /// <summary>How far the blend is, 0..1.</summary>
    public float BlendProgress => _blendLength > 0f ? Mathf.Clamp01(_blendTime / _blendLength) : 1f;

    private CameraPose _from;
    private float _blendTime;
    private float _blendLength;
    private CameraPose _shown;
    private bool _started;
    private float _baseFieldOfView;
    private float _setFieldOfView;

    /// <inheritdoc/>
    public override void OnStart()
    {
        // Rigs without a field of view of their own use the camera's.
        _baseFieldOfView = _setFieldOfView = Entity.GetField("Camera", "Zoom", 55f);
    }

    /// <inheritdoc/>
    public override void OnLateUpdate(float deltaTime)
    {
        VirtualCamera? best = null;
        foreach (var rig in ScriptBridge.AllScripts<VirtualCamera>())
        {
            if (!rig.Entity.IsAlive || !rig.Entity.Active) continue;
            if (best == null || rig.Priority >= best.Priority) best = rig;
        }
        if (best == null) return;
        var target = best.Pose.FieldOfView > 0f ? best.Pose : best.Pose with { FieldOfView = _baseFieldOfView };
        if (best != Active)
        {
            float blend = best.BlendSeconds < 0f ? DefaultBlend : best.BlendSeconds;
            _from = _started ? _shown : target;
            _blendTime = 0f;
            _blendLength = _started ? blend : 0f;
            Active = best;
            _started = true;
        }
        _blendTime += deltaTime;
        var pose = CameraRigMath.Blend(_from, target, CameraRigMath.SmoothStep(BlendProgress));
        _shown = pose;
        var shake = CameraImpulse.Sample(deltaTime);
        transform.WorldPosition = pose.Position + shake;
        transform.LookDirection(pose.Forward, pose.Up);
        if (pose.FieldOfView > 0f && MathF.Abs(pose.FieldOfView - _setFieldOfView) > 1e-4f)
        {
            Entity.SetField("Camera", "Zoom", pose.FieldOfView);
            _setFieldOfView = pose.FieldOfView;
        }
    }
}

/// <summary>
/// Camera shake: Shake(strength, seconds) adds a decaying wobble to whatever the brain shows (none
/// while the player's Reduce motion setting is on).
/// </summary>
public static class CameraImpulse
{
    private static float _strength;
    private static float _left;
    private static float _length;
    private static float _time;
    /// <summary>Wobbles per second.</summary>
    public static float Frequency = 18f;

    /// <summary>Adds a decaying shake of that strength for seconds (a stronger running shake keeps its strength).</summary>
    public static void Shake(float strength = 0.25f, float seconds = 0.4f)
    {
        _strength = Mathf.Max(_strength * (_length > 0f ? _left / _length : 0f), strength);
        _left = _length = Mathf.Max(seconds, 0.01f);
    }

    /// <summary>Whether a shake is running.</summary>
    public static bool IsShaking => _left > 0f;

    internal static Vector3 Sample(float deltaTime)
    {
        if (_left <= 0f) return Vector3.Zero;
        _time += deltaTime;
        _left = Mathf.Max(_left - deltaTime, 0f);
        // Accessibility (PLAY-7): the shake still runs its course, unseen.
        if (Settings.ReduceMotion) return Vector3.Zero;
        float amount = _strength * (_left / _length);
        return new Vector3(MathF.Sin(_time * Frequency * 1.3f), MathF.Sin(_time * Frequency * 1.7f + 1.1f), MathF.Sin(_time * Frequency + 2.3f) * 0.5f) * amount;
    }
}

/// <summary>The rigs' math, exposed for custom rigs and tests.</summary>
public static class CameraRigMath
{
    /// <summary>
    /// The FollowCamera convention: yaw about +Y from +Z, pitch looking down.
    /// </summary>
    public static Vector3 OrbitForward(float yawDegrees, float pitchDegrees)
    {
        float yaw = yawDegrees * Mathf.Deg2Rad;
        float pitch = pitchDegrees * Mathf.Deg2Rad;
        return new Vector3(Mathf.Sin(yaw) * Mathf.Cos(pitch), -Mathf.Sin(pitch), Mathf.Cos(yaw) * Mathf.Cos(pitch));
    }

    /// <summary>
    /// Moves current only as far as needed to keep target within +-zone of it.
    /// </summary>
    public static float DeadZone(float current, float target, float zone)
    {
        if (target > current + zone) return target - zone;
        if (target < current - zone) return target + zone;
        return current;
    }

    /// <summary>3t² − 2t³ for t in 0..1.</summary>
    public static float SmoothStep(float t)
    {
        t = Mathf.Clamp01(t);
        return t * t * (3f - 2f * t);
    }

    /// <summary>Blends two poses: a at t = 0, b at t = 1 (the brain smoothsteps t first).</summary>
    public static CameraPose Blend(CameraPose a, CameraPose b, float t)
    {
        Vector3 forward = Vector3.Lerp(a.Forward, b.Forward, t);
        forward = forward.LengthSquared() > 1e-8f ? forward.Normalized() : b.Forward;
        Vector3 up = Vector3.Lerp(a.Up, b.Up, t);
        up = up.LengthSquared() > 1e-8f ? up.Normalized() : b.Up;
        float fov = a.FieldOfView > 0f && b.FieldOfView > 0f ? Mathf.Lerp(a.FieldOfView, b.FieldOfView, t) : b.FieldOfView;
        return new CameraPose(Vector3.Lerp(a.Position, b.Position, t), forward, up, fov);
    }
}
