using ScriptCore;
using Xunit;

// MOD-2: two script classes with one name (a mod's and the game's) both resolve.
public class RegistryTwin : Script { }

namespace RegistryTestMod
{
    public class RegistryTwin : Script { }
}

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
            ScriptRegistry.ScanMod(typeof(RegistryTwin).Assembly);
            Assert.Equal(count * 2, ScriptRegistry.ScriptTypes.Count);
            // The game's (first scanned) wins a short name.
            Assert.Same(ScriptRegistry.ScriptTypes[0], ScriptRegistry.Find(ScriptRegistry.ScriptTypes[0].Name));
        }
        finally
        {
            ScriptRegistry.Clear();
        }
    }
}
