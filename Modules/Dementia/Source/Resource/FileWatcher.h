#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Watches directory trees for file changes on a background thread (portable mtime polling; an
// asset tree of a few thousand files costs ~1 ms per scan). Changes are collected and handed to the
// main thread via ConsumeChanges(), so reactions (reloads) run where the engine expects them.
// Build artefacts (.bin, .d, .dds, .assbin...) are ignored so exports don't retrigger themselves.
class FileWatcher
{
public:
    enum class ChangeType : uint8_t
    {
        Added,
        Modified,
        Removed
    };

    struct Change
    {
        std::string FullPath;   // generic (forward slash) absolute path
        ChangeType Type = ChangeType::Modified;
    };

    FileWatcher() = default;
    ~FileWatcher();
    FileWatcher( const FileWatcher& ) = delete;
    FileWatcher& operator=( const FileWatcher& ) = delete;

    void Start( const std::vector<std::string>& InRoots, std::chrono::milliseconds InInterval = std::chrono::milliseconds( 500 ) );
    void Stop();
    bool IsRunning() const { return m_running.load(); }

    // Changes since the last call, oldest first. Duplicate events for one file are merged.
    std::vector<Change> ConsumeChanges();

    // Runs one scan synchronously (tests / manual refresh).
    void ScanNow();

    static bool IsIgnored( const std::filesystem::path& InPath );

private:
    void ThreadMain();
    void Scan( bool InIsInitial );

    std::vector<std::filesystem::path> m_roots;
    std::chrono::milliseconds m_interval{ 500 };
    std::unordered_map<std::string, std::filesystem::file_time_type> m_known;

    std::mutex m_mutex;
    std::vector<Change> m_pending;
    // Serializes scans (background thread vs ScanNow).
    std::mutex m_scanMutex;

    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    std::mutex m_sleepMutex;
    std::condition_variable m_sleepCondition;
};
