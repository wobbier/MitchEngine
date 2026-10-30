using System;

namespace ScriptCore.Kit;

/// <summary>
/// A door that slides by OpenOffset (world units) over Duration when used, optionally locked until
/// the user's Inventory has RequiredItem. Give it a kinematic Rigidbody so its collider moves with it.
/// </summary>
public class Door : Interactable
{
    /// <summary>How far it moves to open (local space), metres.</summary>
    public Vector3 OpenOffset = new Vector3(0f, 2.5f, 0f);
    /// <summary>Seconds to open or close.</summary>
    public float Duration = 0.5f;
    /// <summary>An item the interactor needs to open it (empty: none).</summary>
    public string RequiredItem = "";
    /// <summary>Whether opening uses up the item.</summary>
    public bool ConsumeItem = false;
    /// <summary>Open when the game starts.</summary>
    public bool StartsOpen = false;

    /// <summary>Whether it's open (or opening).</summary>
    public bool IsOpen { get; private set; }
    /// <summary>
    /// 0 closed .. 1 open.
    /// </summary>
    public float Progress { get; private set; }
    private Vector3 _closed;

    /// <summary>A door that says "Open".</summary>
    public Door() { Prompt = "Open"; }

    /// <inheritdoc/>
    public override void OnStart()
    {
        _closed = transform.WorldPosition;
        IsOpen = StartsOpen;
        Progress = StartsOpen ? 1f : 0f;
        Place();
    }

    /// <inheritdoc/>
    public override string GetPrompt(Entity by)
    {
        if (IsOpen) return "Close";
        return CanUnlock(by) ? Prompt : $"Locked (needs {RequiredItem})";
    }

    /// <inheritdoc/>
    public override void Interact(Entity by)
    {
        if (IsOpen) Close(by); else TryOpen(by);
    }

    /// <summary>Whether by may open it (has the RequiredItem, if any).</summary>
    public bool CanUnlock(Entity by)
    {
        if (string.IsNullOrEmpty(RequiredItem)) return true;
        return by.GetScript<IInventory>()?.Inventory.Has(RequiredItem) ?? false;
    }

    /// <summary>Opens it if by can unlock it (true when it's already open); false, with a locked DoorEvent, when they can't.</summary>
    public bool TryOpen(Entity by = default)
    {
        if (IsOpen) return true;
        if (!CanUnlock(by))
        {
            EventBus.Publish(new DoorEvent(Entity, by, false, true));
            return false;
        }
        if (ConsumeItem && !string.IsNullOrEmpty(RequiredItem))
        {
            by.GetScript<IInventory>()?.Inventory.Remove(RequiredItem);
            RequiredItem = "";
        }
        IsOpen = true;
        EventBus.Publish(new DoorEvent(Entity, by, true, false));
        return true;
    }

    /// <summary>Closes it.</summary>
    public void Close(Entity by = default)
    {
        if (!IsOpen) return;
        IsOpen = false;
        EventBus.Publish(new DoorEvent(Entity, by, false, false));
    }

    /// <inheritdoc/>
    public override void OnUpdate(float deltaTime)
    {
        float target = IsOpen ? 1f : 0f;
        if (Progress == target) return;
        float step = Duration > 0f ? deltaTime / Duration : 1f;
        Progress = target > Progress ? MathF.Min(Progress + step, 1f) : MathF.Max(Progress - step, 0f);
        Place();
    }

    private void Place()
    {
        float eased = Progress * Progress * (3f - 2f * Progress);
        var position = _closed + OpenOffset * eased;
        if (HasComponent<Rigidbody>())
        {
            GetComponent<Rigidbody>().Teleport(position, transform.Forward, transform.Up);
        }
        else
        {
            transform.WorldPosition = position;
        }
    }
}
