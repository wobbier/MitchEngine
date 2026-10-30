#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

// PLAY-10: the developer console (not in Retail builds' UI). Commands and variables register from
// C++ (Register / RegisterVariable) and C# (a static method with [ConsoleCommand("name", "help")]
// taking string[] and returning a string or nothing; ScriptCore finds them in the game's and the
// mods' assemblies). Game builds open it with ~ (an ImGui window with history and Tab completion);
// the edscript 'console <line>' runs a line anywhere, and --console "<line>" opens it at start-up
// running that line.
//
// Built in: help [prefix], echo <text>, clear, and the variables timescale and maxfps
// ("timescale" prints it, "timescale 0.5" sets it).
class DevConsole
{
public:
    using Handler = std::function<std::string( const std::vector<std::string>& InArgs )>;

    static DevConsole& Get();

    void Register( const std::string& InName, const std::string& InHelp, Handler InHandler );
    void RegisterVariable( const std::string& InName, const std::string& InHelp, std::function<std::string()> InGet,
        std::function<bool( const std::string& InValue )> InSet );

    // Runs a line: "name arg arg" (double quotes group words). Returns what it printed; the line and
    // its output also go to the history and the log.
    std::string Execute( const std::string& InLine );
    // Every command (C++ and C#) starting with InPrefix, sorted.
    std::vector<std::string> Complete( const std::string& InPrefix ) const;
    const std::vector<std::string>& GetHistory() const { return m_history; }
    const std::string& GetLastOutput() const { return m_lastOutput; }

    static std::vector<std::string> Split( const std::string& InLine );

    // Game builds: ~ toggles the window; draws it while open (ImGui).
    void UpdateAndDraw( bool InTogglePressed );
    bool IsOpen() const { return m_open; }

private:
    DevConsole();
    struct Command
    {
        std::string Help;
        Handler Run;
    };
    std::map<std::string, Command> m_commands;
    std::vector<std::string> m_history;
    std::string m_lastOutput;
    bool m_open = false;
    bool m_focusInput = false;
    char m_input[256] = {};
};
