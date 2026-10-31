#pragma once
#include "Game.h"
#include "Havana.h"
#include "Events/EventReceiver.h"
#include "JSON.h"
#include <vector>
#include <functional>
#include "Editor/EditorAutomation.h"
#include "Editor/UndoStack.h"

// I don't like this
// A game checkout's editor pulls its game's components and cores in through here (resolved
// against the engine's Source/ include path: <game>/Game/Source).
#if USING( ME_GAME_MODULE )
#include "../../Game/Source/ComponentRegistry.h"
#endif
class Engine;

#if USING( ME_EDITOR )

class EditorApp
	: public Game
	, public EventReceiver
{
public:
	EditorApp(int argc, char** argv);
	~EditorApp();

	virtual void OnInitialize() override;

	virtual void OnStart() override;
    virtual void OnUpdate(const UpdateContext& inUpdateContext) override;

	void UpdateCameras();

	virtual void OnEnd() override;

    virtual void PreRender() override;
    virtual void PostRender() override;

	// Play mode. Play snapshots the edited scene in memory; Stop restores it (selection and undo
	// history survive because entity GUIDs are preserved).
	void Play();
	// ED-9: Play without scripts (physics, animation and the other cores run); Stop ends it.
	void Simulate();
	bool IsSimulating() const { return m_isSimulating; }
	void Stop();
	void TogglePause();
	void StepFrame();

	// Scene management. Anything that would discard the current scene asks about unsaved changes.
	void RequestNewScene();

	// ED-6, prefab mode: the prefab alone in a temporary scene (the open scene, its selection and
	// its undo history wait in memory). Save writes the prefab; Close brings the scene back and
	// pushes the saved changes into its instances as one undoable step.
	bool OpenPrefab( const std::string& InPrefabPath );
	bool SavePrefab();
	void ClosePrefab( bool InSaveFirst = false );
	bool IsEditingPrefab() const { return !m_prefabAsset.empty(); }
	const std::string& GetEditingPrefab() const { return m_prefabAsset; }
	void RequestOpenScene( const std::string& InScenePath );
	void RequestOpenSceneDialog();
	void SaveScene( bool InSaveAs );
	// Reopens the editor on another project (Havana --project <root>), after the unsaved-changes
	// prompt. Unattended runs only log the command: they never start a second editor.
	void RequestOpenProject( const std::string& InFolder );
	// File > Project Hub: reopens on the Hub (Havana --hub), after the unsaved-changes prompt.
	// Unattended runs only log the command.
	void RequestProjectHub();
	void RequestQuit();
	bool OnQuitRequested() override;

	const bool IsGameRunning() const;

	const bool IsGamePaused() const;

	std::unique_ptr<Havana> Editor;
	class EditorCore* EditorSceneManager = nullptr;

	bool m_isGameRunning = false;
	bool m_isGamePaused = false;
	std::string InitialLevel;

private:
	void OnNewScene( const class NewSceneEvent& InEvent );
	void OnSceneLoaded( const class SceneLoadedEvent& InEvent );
	void StartGame();
	void StopGame();

	// Runs InAction now, or after the user answers the unsaved-changes prompt.
	void RunWithUnsavedCheck( std::function<void()> InAction );
	void DrawEditorModals();
	void UpdateAutosave( float InDeltaSeconds );
	// ME-234: pauses Play and selects the entity when a script throws (Preferences > General).
	void CheckScriptFaults();
	void CheckForRecovery();
	void ClearAutosave();

	EditorAutomation m_automation;
	std::function<void()> m_pendingAction;
	bool m_openUnsavedPrompt = false;
	bool m_openRecoveryPrompt = false;
	json m_recoveryData;
	std::string m_recoveryScenePath;
	float m_autosaveTimer = 0.f;
	bool m_checkedRecovery = false;
	bool m_hasAutosave = false;

	json m_playSnapshot;
	bool m_isSimulating = false;
	std::vector<uint64_t> m_playSelection;
	std::string m_playSceneFilePath;
	bool m_wasDirtyBeforePlay = false;
	bool m_isRestoringSnapshot = false;
	// Prefab mode.
	std::string m_prefabAsset;
	json m_prefabSceneSnapshot;
	std::string m_prefabScenePath;
	std::vector<uint64_t> m_prefabSceneSelection;
	bool m_prefabSceneWasDirty = false;
	UndoStack::History m_prefabSceneHistory;
	json m_prefabBefore;
	json m_prefabAfter;
	uint64_t m_prefabRootGUID = 0;
};

#endif