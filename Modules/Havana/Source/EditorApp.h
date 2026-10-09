#pragma once
#include "Game.h"
#include "Havana.h"
#include "Events/EventReceiver.h"
#include "JSON.h"
#include <vector>
#include <functional>
#include "Editor/EditorAutomation.h"

// I don't like this
#include "../../Game/Source/ComponentRegistry.h"
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
	void Stop();
	void TogglePause();
	void StepFrame();

	// Scene management. Anything that would discard the current scene asks about unsaved changes.
	void RequestNewScene();
	void RequestOpenScene( const std::string& InScenePath );
	void RequestOpenSceneDialog();
	void SaveScene( bool InSaveAs );
	void RequestQuit();
	bool OnQuitRequested() override;

	const bool IsGameRunning() const;

	const bool IsGamePaused() const;

	std::unique_ptr<Havana> Editor;
	class EditorCore* EditorSceneManager = nullptr;

	virtual bool OnEvent(const BaseEvent& evt) override;
	bool m_isGameRunning = false;
	bool m_isGamePaused = false;
	std::string InitialLevel;

private:
	void StartGame();
	void StopGame();

	// Runs InAction now, or after the user answers the unsaved-changes prompt.
	void RunWithUnsavedCheck( std::function<void()> InAction );
	void DrawEditorModals();
	void UpdateAutosave( float InDeltaSeconds );
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

	json m_playSnapshot;
	std::vector<uint64_t> m_playSelection;
	std::string m_playSceneFilePath;
	bool m_wasDirtyBeforePlay = false;
	bool m_isRestoringSnapshot = false;
};

#endif