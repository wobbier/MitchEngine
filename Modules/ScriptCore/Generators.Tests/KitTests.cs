using ScriptCore;
using ScriptCore.Kit;
using Xunit;

// GAME-7: the starter kit's plain parts (the scripts are exercised in the template's flow).
public class KitTests
{
    [Fact]
    public void Health_takes_damage_dies_once_heals_and_resets()
    {
        var health = new Health(50f);
        int died = 0;
        float taken = 0f;
        health.Died += _ => ++died;
        health.Damaged += (amount, _) => taken += amount;
        using var subscription = EventBus.Subscribe<DiedEvent>(_ => ++died);
        Assert.Equal(20f, health.Damage(Entity.Null, 20f));
        Assert.Equal(10f, health.Heal(Entity.Null, 15f) - 5f);
        Assert.Equal(45f, health.Current);
        health.Invulnerable = true;
        Assert.Equal(0f, health.Damage(Entity.Null, 100f));
        health.Invulnerable = false;
        Assert.Equal(45f, health.Damage(Entity.Null, 100f));   // only what was left
        Assert.True(health.IsDead);
        Assert.Equal(0f, health.Damage(Entity.Null, 10f));
        Assert.Equal(0f, health.Heal(Entity.Null, 10f));      // the dead don't heal
        Assert.Equal(2, died);                                 // the event and the bus, once each
        Assert.Equal(65f, taken);
        health.Reset();
        Assert.Equal(50f, health.Current);
    }

    [Fact]
    public void Inventory_counts_items()
    {
        var inventory = new Inventory();
        inventory.Add("Key");
        inventory.Add("Coin", 3);
        inventory.Add("", 5);
        Assert.True(inventory.Has("Key"));
        Assert.True(inventory.Has("Coin", 3));
        Assert.False(inventory.Has("Coin", 4));
        Assert.True(inventory.Remove("Coin", 2));
        Assert.False(inventory.Remove("Coin", 2));
        Assert.Equal(1, inventory.Count("Coin"));
        Assert.True(inventory.Remove("Key"));
        Assert.False(inventory.Has("Key"));
        Assert.Equal(1, inventory.Items.Count);
    }
}

// GAME-1: the camera rigs' math.
public class CameraRigTests
{
    [Fact]
    public void Blends_ease_and_keep_aims_unit_length()
    {
        var a = new CameraPose(new Vector3(0f, 5f, -10f), new Vector3(0f, 0f, 1f), Vector3.Up, 55f);
        var b = new CameraPose(new Vector3(4f, 2f, -4f), new Vector3(1f, 0f, 0f), Vector3.Up, 40f);
        Assert.Equal(a.Position, CameraRigMath.Blend(a, b, 0f).Position);
        Assert.Equal(b.Position, CameraRigMath.Blend(a, b, 1f).Position);
        var mid = CameraRigMath.Blend(a, b, 0.5f);
        Assert.InRange(mid.Forward.Length(), 0.999f, 1.001f);
        Assert.Equal(47.5f, mid.FieldOfView, 3);
        Assert.Equal(0.5f, CameraRigMath.SmoothStep(0.5f), 4);
        Assert.True(CameraRigMath.SmoothStep(0.1f) < 0.1f);
    }

    [Fact]
    public void Dead_zones_and_orbits()
    {
        Assert.Equal(0f, CameraRigMath.DeadZone(0f, 0.5f, 1f));
        Assert.Equal(1f, CameraRigMath.DeadZone(0f, 2f, 1f));
        Assert.Equal(-1f, CameraRigMath.DeadZone(0f, -2f, 1f));
        var forward = CameraRigMath.OrbitForward(0f, 0f);
        Assert.InRange(forward.z, 0.999f, 1.001f);
        Assert.True(CameraRigMath.OrbitForward(0f, 30f).y < 0f);   // pitch looks down
    }
}
