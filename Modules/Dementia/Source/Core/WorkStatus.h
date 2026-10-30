#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// ED-14: what the engine is working on in the background (script builds, shader and asset
// reimports, cooks, bakes) and short notices about how it went, for the editor's status
// indicator and toasts. Thread-safe: workers begin and end their own tasks.
class WorkStatus
{
public:
    struct Task
    {
        uint64_t Id = 0;
        std::string Label;
        float Progress = -1.f;  // 0..1, or < 0 while unknown
    };
    enum class Level : uint8_t
    {
        Info,
        Warning,
        Error,
    };
    struct Toast
    {
        Level Severity = Level::Info;
        std::string Message;
        std::string ActionId;   // an editor action that fixes or shows it ("" for none)
        std::string ActionLabel;
    };

    static WorkStatus& Get();

    uint64_t Begin( const std::string& InLabel );
    void SetProgress( uint64_t InId, float InProgress );
    void End( uint64_t InId );
    std::vector<Task> GetTasks() const;

    void Notify( Level InLevel, const std::string& InMessage, const std::string& InActionId = {}, const std::string& InActionLabel = {} );
    std::vector<Toast> TakeToasts();

    // Begin / End for a scope.
    class Scope
    {
    public:
        explicit Scope( const std::string& InLabel ) : m_id( WorkStatus::Get().Begin( InLabel ) ) {}
        ~Scope() { WorkStatus::Get().End( m_id ); }
        void SetProgress( float InProgress ) { WorkStatus::Get().SetProgress( m_id, InProgress ); }
        Scope( const Scope& ) = delete;
        Scope& operator=( const Scope& ) = delete;

    private:
        uint64_t m_id;
    };

private:
    mutable std::mutex m_mutex;
    std::vector<Task> m_tasks;
    std::vector<Toast> m_toasts;
    uint64_t m_nextId = 1;
};
