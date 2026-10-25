#pragma once
#include <string>
#include <vector>

// Recorded dependencies (--record-deps <file>): every logical path a run read through the VFS
// (VFS::SetRecordingReads), written when the run exits as { "Version": 1, "Reads": [...] }.
// The cooker (Cooker.h) ships them, so assets a script or engine code loads by a string path
// no scene names still make it into the cooked build.
namespace Cook
{
    // Engine::Init: starts recording when the command line asks for it.
    void StartRecordingIfRequested();
    // Engine::Run's exit: writes what the run read to the --record-deps file.
    void WriteRecordingIfRequested();

    bool WriteDependencies( const std::string& InFile, const std::vector<std::string>& InReads );
    bool ReadDependencies( const std::string& InFile, std::vector<std::string>& OutReads );
}
