namespace ScriptCore;

/// <summary>
/// The entity's ParticleSystem (CS-5 c). Play restarts emission, Stop ends it (live particles
/// finish their lives), Burst emits a number at once.
/// </summary>
public class ParticleSystem : Component
{
    public void Play() { unsafe { Engine._api.Particles_Play(Entity); } }
    public void Stop() { unsafe { Engine._api.Particles_Stop(Entity); } }
    public void Burst(int count) { unsafe { Engine._api.Particles_Burst(Entity, count); } }
    public int ParticleCount { get { unsafe { return Engine._api.Particles_GetCount(Entity); } } }
}
