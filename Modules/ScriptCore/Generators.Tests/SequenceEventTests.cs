using System.Collections.Generic;
using System.Reflection;
using ScriptCore;
using Xunit;

// GAME-4: the engine's "Sequence" events arrive as typed SequenceEvents on the EventBus.
public class SequenceEventTests
{
    private static void Dispatch(string json) =>
        typeof(EventBus).GetMethod("DispatchEngine", BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, new object[] { json });

    [Fact]
    public void Engine_sequence_events_publish_typed_markers_and_notifications()
    {
        var heard = new List<SequenceEvent>();
        using var subscription = EventBus.Subscribe<SequenceEvent>(heard.Add);
        Dispatch("""
            [ { "Type": "Sequence", "Player": [3, 1], "Receiver": [7, 2], "Kind": 0, "Name": "Open", "Payload": "fast", "Number": 2.5, "Frame": 30, "Clip": "Assets/Gate.sequence" },
              { "Type": "Sequence", "Player": [3, 1], "Receiver": [0, 0], "Kind": 2, "Name": "Gate", "Payload": "", "Number": 0, "Frame": 60, "Clip": "" } ]
            """);
        Assert.Equal(2, heard.Count);
        Assert.Equal(new SequenceEvent(new Entity(3, 1), new Entity(7, 2), SequenceEventKind.Marker, "Open", "fast", 2.5f, 30, "Assets/Gate.sequence"), heard[0]);
        Assert.Equal(SequenceEventKind.Finished, heard[1].Kind);
        Assert.Equal(Entity.Null, heard[1].Receiver);
    }
}
