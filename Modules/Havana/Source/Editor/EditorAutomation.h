#pragma once
#include <cstdint>
#include "Dementia.h"
#include "JSON.h"
#include <string>
#include <vector>

#if USING( ME_EDITOR )

class EditorApp;

// Runs an editor script given with --editor-exec <file>: one command per line, executed one per
// frame after the first scene has loaded. Lets editor workflows (select, duplicate, undo,
// play/stop, save...) be tested unattended. Summary and failures go to the log and, with
// --editor-exec-result <file>, to a JSON report. See Docs/Editor-Havana.md for the command list.
class EditorAutomation
{
public:
    void Init();
    bool IsActive() const { return m_isActive; }
    void Tick( EditorApp& InApp );

private:
    bool Execute( EditorApp& InApp, const std::string& InLine );
    void Fail( const std::string& InMessage );
    void Finish();

    std::vector<std::string> m_lines;
    size_t m_nextLine = 0;
    int m_waitFrames = 10;
    int m_assetPreviewWaitFrames = 0;
    // wait-log: the text to wait for, frames left, and the log position it started at.
    std::string m_waitLogText;
    int m_waitLogFrames = 0;
    uint64_t m_waitLogFrom = 0;
    // wait-count-stable: frames the entity count must hold, frames left before giving up.
    int m_stableFramesNeeded = 0;
    int m_stableTimeoutFrames = 0;
    int m_stableRun = 0;
    size_t m_stableLastCount = 0;
    // wait-field: the assert-field condition to wait for, frames left before giving up.
    std::string m_waitFieldEntity;
    std::string m_waitFieldPath;
    std::string m_waitFieldValue;
    int m_waitFieldFrames = 0;
    // Log message count when the previous command ran: wait-log also sees what that command logged.
    uint64_t m_previousCommandLogCount = 0;
    bool m_lastWasWaitLog = false;     // consecutive wait-logs share one window
    uint64_t m_commandLogCount = 0;
    int m_failures = 0;
    size_t m_markedCount = 0;
    std::vector<std::string> m_failureMessages;
    std::string m_resultPath;
    bool m_isActive = false;
    bool m_isFinished = false;
    bool m_recordProfile = false;
    json m_profileFrames = json::array();
};

#endif
