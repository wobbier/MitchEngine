using System.Linq;
using System.Threading.Tasks;
using ScriptCore;
using Xunit;

// CS-12: [GameTest] discovery (running them needs the engine: the template flow does that).
public static class SampleGameTests
{
    [GameTest("Assets/Scenes/Main.lvl", TimeoutSeconds = 5f)]
    public static Task Loads_its_scene() => Task.CompletedTask;

    [GameTest]
    public static void Runs_in_place() { }

    [GameTest]
    public static Task Takes_an_argument(int value) => Task.CompletedTask;   // rejected: parameters

    public static Task Not_a_test() => Task.CompletedTask;
}

// Shares the static ScriptRegistry with the other registry tests: one at a time.
[Collection("ScriptRegistry")]
public class GameTestDiscoveryTests
{
    [Fact]
    public void Finds_static_tests_by_class_then_name_and_filters()
    {
        ScriptRegistry.Scan(typeof(SampleGameTests).Assembly);
        var names = GameTests.Discover().Select(GameTests.NameOf).Where(n => n.StartsWith("SampleGameTests.")).ToList();
        Assert.Equal(new[] { "SampleGameTests.Loads_its_scene", "SampleGameTests.Runs_in_place" }, names);
        Assert.Equal(new[] { "SampleGameTests.Runs_in_place" }, GameTests.Discover("in_place").Select(GameTests.NameOf).ToArray());
        var attribute = typeof(SampleGameTests).GetMethod("Loads_its_scene")!.GetCustomAttributes(typeof(GameTestAttribute), false).Cast<GameTestAttribute>().Single();
        Assert.Equal(5f, attribute.TimeoutSeconds);
    }

    [Fact]
    public void Asserts_explain_what_failed()
    {
        Assert.Equal("score: expected 3, got 2", Assert.Throws<GameTestFailure>(() => GameAssert.Equal(3, 2, "score")).Message);
        Assert.Throws<GameTestFailure>(() => GameAssert.Near(1f, 1.5f, 0.1f));
        GameAssert.Near(1f, 1.05f, 0.1f);
        GameAssert.True(true);
    }
}
