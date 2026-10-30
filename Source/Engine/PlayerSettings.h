#pragma once
#include "Dementia.h"
#include "JSON.h"
#include <cstdint>
#include <string>

class AudioCore;
class HardwareBudget;
class InputActionSystem;
class RmlUiCore;

// The player's settings (PLAY-2): one versioned JSON document in user://Settings.json, read at
// boot and applied to the engine's systems. Everything is a dotted key:
//
//   Graphics.Quality            "" (no player caps) or a HardwareBudget quality preset ("Low".."Ultra")
//   Graphics.RenderScale        0.5..1, a scale on every game camera's render scale (ME-275)
//   Graphics.DynamicResolution  bool, dynamic resolution on every game camera
//   Graphics.Upscale            "" (each camera's own) or Bilinear / Nearest / Pixel / Sharp
//   Audio.Master|Music|SFX|UI|Voice   0..1, scaling the project's bus mix
//   Language                    "" or a string table code (Assets/UI/Strings/<code>.json)
//   Accessibility.TextScale     0.5..3, the UI's dp text scale
//   Accessibility.ReduceMotion  bool: no camera shake (the kit's CameraImpulse); games read it for sway
//   Accessibility.HighContrast  bool, a high-contrast class on every UI document (PLAY-7)
//   Accessibility.Subtitles     bool, captions and subtitles (C# Captions) on
//   Accessibility.SpeakerNames  bool, captions name who's speaking
//   Accessibility.SubtitleSize  0.75..2, caption text scale
//   Accessibility.SubtitleBackground  0..1, the caption backdrop's opacity
//   Input.Bindings              the game input's rebinds (InputActionSystem overrides)
//   Game.<anything>             the game's own values, any JSON
//
// Unattended runs (captures, editor scripts) keep the settings in memory only, so a player's
// file never leaks into them; --user-dir <folder> points user:// at a folder for the run and
// opts it into loading and saving (two-launch flows prove settings survive a restart).
// Main thread only.
class PlayerSettings
{
public:
    static constexpr int kVersion = 1;
    static constexpr const char* kDefaultPath = "user://Settings.json";

    // What the settings drive; any of them may be null (tests drive only what they own).
    struct Targets
    {
        InputActionSystem* Actions = nullptr;
        AudioCore* Audio = nullptr;
        RmlUiCore* UI = nullptr;
        HardwareBudget* Budget = nullptr;
    };

    PlayerSettings();
    static PlayerSettings& Get();

    // Every key at its default value.
    static json Defaults();
    // A stored document read over the defaults: older versions migrate, unknown keys outside
    // Game are dropped, values of the wrong type keep their default and numbers clamp to their
    // range. OutNotes (optional) says what was dropped or fixed.
    static json Sanitize( const json& InStored, std::string* OutNotes = nullptr );

    void SetTargets( const Targets& InTargets );
    // Where Load and Save go ("" = in memory only).
    void SetPath( const std::string& InPath );
    const std::string& GetPath() const { return m_path; }
    bool IsPersistent() const { return !m_path.empty(); }

    // Boot: picks the path for this run (see above), loads and applies.
    void Init( const Targets& InTargets );
    // Reads the file over the defaults and applies every value (a missing file reads as the
    // defaults). False when the file exists but can't be read.
    bool Load();
    // Writes the document atomically (a temporary file, then a rename). False when in memory only.
    bool Save();
    // Back to the defaults (bindings cleared) and applied; saved by the next Update.
    void Reset();
    // Once a frame: writes unsaved changes once they've settled (a dragged slider writes once).
    void Update();
    bool IsDirty() const { return m_dirty; }
    // Counts changes (sets, loads, resets, rebinds): views redraw when it moves.
    uint64_t GetRevision() const { return m_revision; }

    // Null for a key that isn't a setting.
    json GetValue( const std::string& InKey ) const;
    // Checks InValue against the key's type and range, stores and applies it. False (with
    // OutError) when the key or the value is rejected.
    bool SetValue( const std::string& InKey, const json& InValue, std::string* OutError = nullptr );

    // Rebinds a control of the game input (see InputActionSystem::SetBindingOverride) and keeps
    // it with the settings.
    bool Rebind( const std::string& InAction, size_t InBinding, const std::string& InPart, const std::string& InControl );
    void ResetBindings();
    // The game input's current rebinds were changed elsewhere (an interactive rebind): keep them.
    void CaptureBindings();

    const json& GetDocument() const { return m_document; }

private:
    void Apply();
    void ApplyKey( const std::string& InKey );
    void MarkDirty();

    json m_document;
    std::string m_path;
    Targets m_targets;
    bool m_dirty = false;
    double m_changedAt = 0.0;
    uint64_t m_revision = 0;
};
