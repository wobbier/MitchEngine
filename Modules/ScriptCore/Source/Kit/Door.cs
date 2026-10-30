using System;

namespace ScriptCore.Kit;

// A door that slides by OpenOffset (world units) over Duration when used, optionally locked until
// the user's Inventory has RequiredItem. Give it a kinematic Rigidbody so its collider moves with it.
public class Door : Interactable
{
    public Vector3 OpenOffset = new Vector3(0f, 2.5f, 0f);
    public float Duration = 0.5f;
    public string RequiredItem = "";
    public bool ConsumeItem = false;
    public bool StartsOpen = false;

    public bool IsOpen { get; private set; }
    // 0 closed .. 1 open.
    public float Progress { get; private set; }
    private Vector3 _closed;

    public Door() { Prompt = "Open"; }

    public override void OnStart()
    {
        _closed = transform.WorldPosition;
        IsOpen = StartsOpen;
        Progress = StartsOpen ? 1f : 0f;
        Place();
    }

    public override string GetPrompt(Entity by)
    {
        if (IsOpen) return "Close";
        return CanUnlock(by) ? Prompt : $"Locked (needs {RequiredItem})";
    }

    public override void Interact(Entity by)
    {
        if (IsOpen) Close(by); else TryOpen(by);
    }

    public bool CanUnlock(Entity by)
    {
        if (string.IsNullOrEmpty(RequiredItem)) return true;
        return by.GetScript<IInventory>()?.Inventory.Has(RequiredItem) ?? false;
    }

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

    public void Close(Entity by = default)
    {
        if (!IsOpen) return;
        IsOpen = false;
        EventBus.Publish(new DoorEvent(Entity, by, false, false));
    }

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
