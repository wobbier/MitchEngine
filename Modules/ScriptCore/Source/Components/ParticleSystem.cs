namespace ScriptCore;

/// <summary>
/// The entity's ParticleSystem (CS-5 c). Play restarts emission, Stop ends it (live particles
/// finish their lives), Burst emits a number at once.
/// </summary>
public class ParticleSystem : Component
{
    /// <summary>Restarts emission.</summary>
    public void Play() { unsafe { Engine._api.Particles_Play(Entity); } }
    /// <summary>Stops emitting; live particles finish their lives.</summary>
    public void Stop() { unsafe { Engine._api.Particles_Stop(Entity); } }
    /// <summary>Emits count particles at once.</summary>
    public void Burst(int count) { unsafe { Engine._api.Particles_Burst(Entity, count); } }
    /// <summary>How many particles are alive.</summary>
    public int ParticleCount { get { unsafe { return Engine._api.Particles_GetCount(Entity); } } }
}
