using System;

namespace ScriptCore.Kit;

// GAME-7 starter kit. An entity carries one script, so what an actor *has* (health, an
// inventory, an interactor) is a plain object its script owns and exposes through IHealth /
// IInventory; the world pieces (Pickup, Door, DamageZone, Checkpoint, Spawner, Damageable) are
// scripts that find those on whatever touches them:
//   public class Player : Script, IHealth, IInventory
//   {
//       public Health Health { get; } = new(100f);
//       public Inventory Inventory { get; } = new();
//   }

/// <summary>An actor with health (Damageable and the player).</summary>
public interface IHealth
{
    /// <summary>The actor's health.</summary>
    Health Health { get; }
}
/// <summary>An actor with an inventory.</summary>
public interface IInventory
{
    /// <summary>The actor's inventory.</summary>
    Inventory Inventory { get; }
}

/// <summary>
/// Published on the EventBus.
/// </summary>
public readonly record struct DamagedEvent(Entity Target, float Amount, float Remaining, Entity Source);
/// <summary>An actor's health reached zero.</summary>
/// <param name="Target">Who died.</param>
/// <param name="Source">Who did it (Entity.Null when unknown).</param>
public readonly record struct DiedEvent(Entity Target, Entity Source);
/// <summary>An actor was healed.</summary>
/// <param name="Target">Who was healed.</param>
/// <param name="Amount">How much it actually gained.</param>
/// <param name="Remaining">Its health now.</param>
public readonly record struct HealedEvent(Entity Target, float Amount, float Remaining);
/// <summary>Something was picked up.</summary>
/// <param name="Collector">Who picked it up.</param>
/// <param name="Pickup">The pickup.</param>
/// <param name="Item">The item's name.</param>
/// <param name="Amount">How many.</param>
public readonly record struct PickedUpEvent(Entity Collector, Entity Pickup, string Item, int Amount);
/// <summary>An actor reached a checkpoint.</summary>
/// <param name="Actor">Who reached it.</param>
/// <param name="Checkpoint">The checkpoint.</param>
public readonly record struct CheckpointReachedEvent(Entity Actor, Entity Checkpoint);
/// <summary>A door opened or closed, or someone tried a locked one.</summary>
/// <param name="Door">The door.</param>
/// <param name="By">Who.</param>
/// <param name="Opened">Whether it's open now.</param>
/// <param name="Locked">Whether it was locked to them.</param>
public readonly record struct DoorEvent(Entity Door, Entity By, bool Opened, bool Locked);
/// <summary>A spawner spawned something.</summary>
/// <param name="Spawner">The spawner.</param>
/// <param name="Spawned">The new entity.</param>
public readonly record struct SpawnedEvent(Entity Spawner, Entity Spawned);

/// <summary>Health points: Damage, Heal and Kill, with events and EventBus messages.</summary>
public sealed class Health
{
    /// <summary>Full health of max.</summary>
    public Health(float max = 100f)
    {
        Max = max;
        Current = max;
    }

    /// <summary>The most health.</summary>
    public float Max;
    /// <summary>Health now.</summary>
    public float Current;
    /// <summary>While true, damage does nothing.</summary>
    public bool Invulnerable;
    /// <summary>Whether health is at zero.</summary>
    public bool IsDead => Current <= 0f;
    /// <summary>
    /// Fired with the amount actually taken / healed.
    /// </summary>
    public event Action<float, Entity>? Damaged;
    /// <summary>Fires once when health reaches zero, with the source.</summary>
    public event Action<Entity>? Died;

    /// <summary>
    /// Damage(10, attacker) on the entity that owns this health (the events name it).
    /// </summary>
    public float Damage(Entity owner, float amount, Entity source = default)
    {
        if (IsDead || Invulnerable || amount <= 0f)
        {
            return 0f;
        }
        float taken = MathF.Min(amount, Current);
        Current -= taken;
        Damaged?.Invoke(taken, source);
        EventBus.Publish(new DamagedEvent(owner, taken, Current, source));
        if (IsDead)
        {
            Died?.Invoke(source);
            EventBus.Publish(new DiedEvent(owner, source));
        }
        return taken;
    }

    /// <summary>Heals up to Max (not the dead); returns how much it gained.</summary>
    public float Heal(Entity owner, float amount)
    {
        if (IsDead || amount <= 0f)
        {
            return 0f;
        }
        float healed = MathF.Min(amount, Max - Current);
        Current += healed;
        if (healed > 0f)
        {
            EventBus.Publish(new HealedEvent(owner, healed, Current));
        }
        return healed;
    }

    /// <summary>Takes all remaining health.</summary>
    public void Kill(Entity owner, Entity source = default) => Damage(owner, Current, source);

    /// <summary>
    /// Back to full (a respawn).
    /// </summary>
    public void Reset() => Current = Max;
}
