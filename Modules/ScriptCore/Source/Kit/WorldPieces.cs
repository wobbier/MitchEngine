using System;
using System.Collections.Generic;

namespace ScriptCore.Kit;

// Health for props and enemies that have no script of their own (a crate that breaks, a target).
public class Damageable : Script, IHealth
{
    public float MaxHealth = 50f;
    public bool DestroyOnDeath = true;
    public Health Health { get; private set; } = new();

    public override void OnStart()
    {
        Health = new Health(MaxHealth);
        Health.Died += _ => { if (DestroyOnDeath) Entity.Destroy(); };
    }
}

// Hurts anything with health inside its trigger collider: Damage once on entering, then
// DamagePerSecond while inside.
public class DamageZone : Script
{
    public float Damage = 0f;
    public float DamagePerSecond = 20f;
    private readonly HashSet<Entity> _inside = new();

    public override void OnTriggerEnter(Entity other)
    {
        if (other.GetScript<IHealth>() is not IHealth target) return;
        _inside.Add(other);
        target.Health.Damage(other, Damage, Entity);
    }

    public override void OnTriggerExit(Entity other) => _inside.Remove(other);

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

// Where an actor comes back: entering its trigger makes it that actor's respawn point.
// Checkpoint.Respawn(actor) puts the actor there (and refills its health).
public class Checkpoint : Script
{
    public Vector3 Offset = new Vector3(0f, 1f, 0f);
    private static readonly Dictionary<Entity, Vector3> Respawns = new();

    public override void OnTriggerEnter(Entity other)
    {
        if (other.GetScript<IHealth>() == null) return;
        var point = transform.WorldPosition + Offset;
        if (Respawns.TryGetValue(other, out var current) && Vector3.Distance(current, point) < 1e-3f) return;
        Respawns[other] = point;
        EventBus.Publish(new CheckpointReachedEvent(other, Entity));
    }

    public static bool TryGetRespawn(Entity actor, out Vector3 point) => Respawns.TryGetValue(actor, out point);

    // Moves the actor to its last checkpoint (or Fallback when it reached none) and refills it.
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

// Collected by touching its trigger: Item x Amount into the collector's Inventory and/or Heal into
// its Health, then it's gone. Spins while it waits.
public class Pickup : Script
{
    public string Item = "Coin";
    public int Amount = 1;
    public float Heal = 0f;
    public float SpinDegreesPerSecond = 90f;
    private bool _taken;

    public override void OnUpdate(float deltaTime)
    {
        if (SpinDegreesPerSecond != 0f) transform.Rotate(new Vector3(0f, SpinDegreesPerSecond * deltaTime, 0f));
    }

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

// Makes copies of a prefab at its position: one every Interval seconds while fewer than MaxAlive
// of its copies exist, Total in all (0 = no limit).
public class Spawner : Script
{
    public string Prefab = "";
    public float Interval = 3f;
    public int MaxAlive = 3;
    public int Total = 0;
    public bool SpawnOnStart = true;

    public int Spawned { get; private set; }
    private readonly List<Entity> _alive = new();
    private float _timer;

    public int Alive
    {
        get
        {
            _alive.RemoveAll(e => !e.IsAlive);
            return _alive.Count;
        }
    }

    public override void OnStart()
    {
        _timer = SpawnOnStart ? Interval : 0f;
    }

    public override void OnUpdate(float deltaTime)
    {
        _timer += deltaTime;
        if (_timer < Interval) return;
        _timer = 0f;
        Spawn();
    }

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
