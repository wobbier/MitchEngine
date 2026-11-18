#pragma once
#include <string>

// F9 profiler captures: a press records the next kCaptureFrames frames with Optick (.opt, Win64)
// or the ChromeTrace shim (.json, Linux) into .tmp/Captures/, keeping the newest kCapturesKept.
// The engine's frame loop drives it in the editor and in profiling game builds (Debug, Release,
// Profile); where the profiler macros compile to nothing (macOS) a capture saves nothing.
namespace FrameCapture
{
    constexpr int kCaptureFrames = 10;
    constexpr std::size_t kCapturesKept = 5;

    // ".tmp/Captures/<InFileName>", creating the folder. Captures can reach gigabytes each, so
    // they stay out of the project root.
    std::string CapturePath( const std::string& InFileName );

    // Once a frame: starts a capture on InStartPressed (unless one is running) and saves it
    // kCaptureFrames frames later as capture_<date>_<time>.
    void Update( bool InStartPressed );
}
