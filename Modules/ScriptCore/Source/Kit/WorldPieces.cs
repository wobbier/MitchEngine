using System;
using System.Collections.Generic;

namespace ScriptCore.Kit;

/// <summary>
/// Health for props and enemies that have no script of their own (a crate that breaks, a target).
/// </summary>
public class Damageable : Script, IHealth
{
    /// <summary>Health it starts with.</summary>
    public float MaxHealth = 50f;
    /// <summary>Destroy the entity when its health reaches zero.</summary>
    public bool DestroyOnDeath = true;
    /// <summary>Its health.</summary>
    public Health Health { get; private set; } = new();

    /// <inheritdoc/>
    public override void OnStart()
    {
        Health = new Health(MaxHealth);
        Health.Died += _ => { if (DestroyOnDeath) Entity.Destroy(); };
    }
}

/// <summary>
/// Hurts anything with health inside its trigger collider: Damage once on entering, then
/// DamagePerSecond while inside.
/// </summary>
public class DamageZone : Script
{
    /// <summary>Damage once, when something enters.</summary>
    public float Damage = 0f;
    /// <summary>Damage per second while something stays inside.</summary>
    public float DamagePerSecond = 20f;
    private readonly HashSet<Entity> _inside = new();

    /// <inheritdoc/>
    public override void OnTriggerEnter(Entity other)
    {
        if (other.GetScript<IHealth>() is not IHealth target) return;
        _inside.Add(other);
        target.Health.Damage(other, Damage, Entity);
    }

    /// <inheritdoc/>
    public override void OnTriggerExit(Entity other) => _inside.Remove(other);

    /// <inheritdoc/>
    public override void OnUpdate(float deltaTime)
    {
        if (DamagePerSecond <= 0f || _inside.Count == 0) return;
        _inside.RemoveWhere(e => !e.IsAlive);
        foreach (var entity in _inside)
        {
            entity.GetScript<IHealth>()?.Health.Damage(entity, DamagePerSecond * deltaTime, Entity);
        }
    }
}

/// <summary>
/// Where an actor comes back: entering its trigger makes it that actor's respawn point.
/// Checkpoint.Respawn(actor) puts the actor there (and refills its health).
/// </summary>
public class Checkpoint : Script
{
    /// <summary>Where respawns go, from the checkpoint.</summary>
    public Vector3 Offset = new Vector3(0f, 1f, 0f);
    private static readonly Dictionary<Entity, Vector3> Respawns = new();

    /// <inheritdoc/>
    public override void OnTriggerEnter(Entity other)
    {
        if (other.GetScript<IHealth>() == null) return;
        var point = transform.WorldPosition + Offset;
        if (Respawns.TryGetValue(other, out var current) && Vector3.Distance(current, point) < 1e-3f) return;
        Respawns[other] = point;
        EventBus.Publish(new CheckpointReachedEvent(other, Entity));
    }

    /// <summary>The respawn point of the last checkpoint an actor reached; false when none.</summary>
    public static bool TryGetRespawn(Entity actor, out Vector3 point) => Respawns.TryGetValue(actor, out point);

    /// <summary>
    /// Moves the actor to its last checkpoint (or Fallback when it reached none) and refills it.
    /// </summary>
    public static bool Respawn(Entity actor, Vector3? fallback = null)
    {
        if (!TryGetRespawn(actor, out var point))
        {
            if (fallback == null) return false;
            point = fallback.Value;
        }
        actor.Transform.WorldPosition = point;
        actor.GetScript<IHealth>()?.Health.Reset();
        return true;
    }
}

/// <summary>
/// Collected by touching its trigger: Item x Amount into the collector's Inventory and/or Heal into
/// its Health, then it's gone. Spins while it waits.
/// </summary>
public class Pickup : Script
{
    /// <summary>The item it gives.</summary>
    public string Item = "Coin";
    /// <summary>How many.</summary>
    public int Amount = 1;
    /// <summary>Health it restores (0: none).</summary>
    public float Heal = 0f;
    /// <summary>Idle spin, degrees per second.</summary>
    public float SpinDegreesPerSecond = 90f;
    private bool _taken;

    /// <inheritdoc/>
    public override void OnUpdate(float deltaTime)
    {
        if (SpinDegreesPerSecond != 0f) transform.Rotate(new Vector3(0f, SpinDegreesPerSecond * deltaTime, 0f));
    }

    /// <inheritdoc/>
    public override void OnTriggerEnter(Entity other)
    {
        if (_taken) return;
        var inventory = string.IsNullOrEmpty(Item) || Amount <= 0 ? null : other.GetScript<IInventory>();
        var health = Heal > 0f ? other.GetScript<IHealth>() : null;
        // A health pickup waits for someone who needs it.
        bool heals = health != null && health.Health.Current < health.Health.Max;
        if (inventory == null && !heals) return;
        inventory?.Inventory.Add(Item, Amount);
        if (heals) health!.Health.Heal(other, Heal);
        _taken = true;
        EventBus.Publish(new PickedUpEvent(other, Entity, inventory != null ? Item : "", inventory != null ? Amount : 0));
        Entity.Destroy();
    }
}

/// <summary>
/// Makes copies of a prefab at its position: one every Interval seconds while fewer than MaxAlive
/// of its copies exist, Total in all (0 = no limit).
/// </summary>
public class Spawner : Script
{
    /// <summary>The prefab to spawn.</summary>
    public string Prefab = "";
    /// <summary>Seconds between spawns.</summary>
    public float Interval = 3f;
    /// <summary>The most of its spawns alive at once.</summary>
    public int MaxAlive = 3;
    /// <summary>The most spawns in all (0: no limit).</summary>
    public int Total = 0;
    /// <summary>Spawn one as the game starts.</summary>
    public bool SpawnOnStart = true;

    /// <summary>How many it has spawned.</summary>
    public int Spawned { get; private set; }
    private readonly List<Entity> _alive = new();
    private float _timer;

    /// <summary>How many of its spawns are alive.</summary>
    public int Alive
    {
        get
        {
            _alive.RemoveAll(e => !e.IsAlive);
            return _alive.Count;
        }
    }

    /// <inheritdoc/>
    public override void OnStart()
    {
        _timer = SpawnOnStart ? Interval : 0f;
    }

    /// <inheritdoc/>
    public override void OnUpdate(float deltaTime)
    {
        _timer += deltaTime;
        if (_timer < Interval) return;
        _timer = 0f;
        Spawn();
    }

    /// <summary>Spawns one now if the limits allow (Entity.Null if not).</summary>
    public Entity Spawn()
    {
        if (string.IsNullOrEmpty(Prefab) || (Total > 0 && Spawned >= Total) || Alive >= MaxAlive) return default;
        var spawned = World.Instantiate(Prefab);
        if (!spawned.IsAlive) return default;
        spawned.Transform.WorldPosition = transform.WorldPosition;
        _alive.Add(spawned);
        ++Spawned;
        EventBus.Publish(new SpawnedEvent(Entity, spawned));
        return spawned;
    }
}
