using System.Linq;
using ScriptCore;
using Xunit;

// MOD-2: two script classes with one name (a mod's and the game's) both resolve.
public class RegistryTwin : Script { }

namespace RegistryTestMod
{
    public class RegistryTwin : Script { }
}

[Collection("ScriptRegistry")]
public class ScriptRegistryTests
{
    [Fact]
    public void Full_names_tell_same_named_scripts_apart_and_short_names_pick_the_first()
    {
        ScriptRegistry.Scan(typeof(RegistryTwin).Assembly);
        try
        {
            Assert.Equal(typeof(RegistryTwin), ScriptRegistry.Find("RegistryTwin"));
            Assert.Equal(typeof(RegistryTestMod.RegistryTwin), ScriptRegistry.Find("RegistryTestMod.RegistryTwin"));
            // Ambiguous short names show their full name; unique ones stay short.
            Assert.Equal("RegistryTestMod.RegistryTwin", ScriptRegistry.NameOf(typeof(RegistryTestMod.RegistryTwin)));
            Assert.Equal("NetScriptProbe", ScriptRegistry.NameOf(typeof(NetScriptProbe)));
            Assert.Null(ScriptRegistry.Find("NoSuchScript"));
        }
        finally
        {
            ScriptRegistry.Clear();
        }
    }


    [Fact]
    public void A_mod_assembly_adds_to_the_game_s_scripts()
    {
        ScriptRegistry.Scan(typeof(RegistryTwin).Assembly);
        try
        {
            int count = ScriptRegistry.ScriptTypes.Count;
            int game = count - ScriptRegistry.ScriptTypes.Count(t => t.Assembly == typeof(ScriptRegistry).Assembly);
            ScriptRegistry.ScanMod(typeof(RegistryTwin).Assembly);
            Assert.Equal(count + game, ScriptRegistry.ScriptTypes.Count);
            // The engine's own scripts stay last.
            Assert.Equal(typeof(ScriptRegistry).Assembly, ScriptRegistry.ScriptTypes[^1].Assembly);
            // The game's (first scanned) wins a short name.
            Assert.Same(ScriptRegistry.ScriptTypes[0], ScriptRegistry.Find(ScriptRegistry.ScriptTypes[0].Name));
        }
        finally
        {
            ScriptRegistry.Clear();
        }
    }


    [Fact]
    public void The_engine_s_starter_kit_scripts_are_attachable_and_a_game_class_wins_their_names()
    {
        ScriptRegistry.Scan(typeof(RegistryTwin).Assembly);
        try
        {
            Assert.Equal(typeof(ScriptCore.Kit.Pickup), ScriptRegistry.Find("Pickup"));
            Assert.Equal(typeof(ScriptCore.Kit.Spawner), ScriptRegistry.Find("ScriptCore.Kit.Spawner"));
            Assert.Null(ScriptRegistry.Find("Interactable"));   // abstract
            // A game script named like a kit one is the one a short name finds.
            Assert.Equal(typeof(Door), ScriptRegistry.Find("Door"));
            Assert.Equal(typeof(ScriptCore.Kit.Door), ScriptRegistry.Find("ScriptCore.Kit.Door"));
        }
        finally
        {
            ScriptRegistry.Clear();
        }
    }
}

// A game class with a kit script's name.
public class Door : Script { }
