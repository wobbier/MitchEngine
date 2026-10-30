using ScriptCore;

// Counts collected coins and shows them on the HUD.
public class CoinCounter : Script
{
    public int Coins { get; private set; }

    public override void OnStart()
    {
        UI.Model("hud").Set("coins", 0);
        On<CoinCollected>(coin =>
        {
            Coins += coin.Value;
            UI.Model("hud").Set("coins", Coins);
        });
    }
}
