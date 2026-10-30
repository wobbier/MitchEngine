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

public interface IHealth { Health Health { get; } }
public interface IInventory { Inventory Inventory { get; } }

// Published on the EventBus.
public readonly record struct DamagedEvent(Entity Target, float Amount, float Remaining, Entity Source);
public readonly record struct DiedEvent(Entity Target, Entity Source);
public readonly record struct HealedEvent(Entity Target, float Amount, float Remaining);
public readonly record struct PickedUpEvent(Entity Collector, Entity Pickup, string Item, int Amount);
public readonly record struct CheckpointReachedEvent(Entity Actor, Entity Checkpoint);
public readonly record struct DoorEvent(Entity Door, Entity By, bool Opened, bool Locked);
public readonly record struct SpawnedEvent(Entity Spawner, Entity Spawned);

public sealed class Health
{
    public Health(float max = 100f)
    {
        Max = max;
        Current = max;
    }

    public float Max;
    public float Current;
    public bool Invulnerable;
    public bool IsDead => Current <= 0f;
    // Fired with the amount actually taken / healed.
    public event Action<float, Entity>? Damaged;
    public event Action<Entity>? Died;

    // Damage(10, attacker) on the entity that owns this health (the events name it).
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

    public void Kill(Entity owner, Entity source = default) => Damage(owner, Current, source);

    // Back to full (a respawn).
    public void Reset() => Current = Max;
}
