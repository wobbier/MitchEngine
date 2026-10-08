#include "FileWatcher.h"
#include "optick.h"
#include <algorithm>
#include <unordered_set>


FileWatcher::~FileWatcher()
{
    Stop();
}


void FileWatcher::Start( const std::vector<std::string>& InRoots, std::chrono::milliseconds InInterval )
{
    Stop();
    m_roots.clear();
    for( const std::string& root : InRoots )
    {
        std::error_code ec;
        if( std::filesystem::exists( root, ec ) )
        {
            m_roots.push_back( std::filesystem::absolute( root, ec ) );
        }
    }
    m_interval = InInterval;
    m_known.clear();
    Scan( true );

    m_running.store( true );
    m_thread = std::thread( [this]() { ThreadMain(); } );
}


void FileWatcher::Stop()
{
    if( !m_running.exchange( false ) )
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock( m_sleepMutex );
    }
    m_sleepCondition.notify_all();
    if( m_thread.joinable() )
    {
        m_thread.join();
    }
}


bool FileWatcher::IsIgnored( const std::filesystem::path& InPath )
{
    static const std::unordered_set<std::string> kIgnoredExtensions = {
        ".bin", ".d", ".dds", ".ktx", ".assbin", ".tmp", ".swp", ".swx", ".kate-swp"
    };
    const std::string extension = InPath.extension().string();
    if( kIgnoredExtensions.count( extension ) )
    {
        return true;
    }
    const std::string name = InPath.filename().string();
    // Editor temp files: emacs/vim/JetBrains backups.
    return !name.empty() && ( name.back() == '~' || name.front() == '#' || name.rfind( ".#", 0 ) == 0 );
}


void FileWatcher::Scan( bool InIsInitial )
{
    OPTICK_EVENT( "FileWatcher::Scan" );
    std::lock_guard<std::mutex> scanLock( m_scanMutex );
    std::unordered_map<std::string, std::filesystem::file_time_type> seen;
    seen.reserve( m_known.size() + 16 );
    std::vector<Change> changes;

    for( const std::filesystem::path& root : m_roots )
    {
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it( root, std::filesystem::directory_options::skip_permission_denied, ec );
        const std::filesystem::recursive_directory_iterator end;
        for( ; !ec && it != end; it.increment( ec ) )
        {
            const std::filesystem::directory_entry& entry = *it;
            const std::string name = entry.path().filename().string();
            if( entry.is_directory( ec ) )
            {
                if( name == ".tmp" || name == ".git" || name == ".build" )
                {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if( !entry.is_regular_file( ec ) || IsIgnored( entry.path() ) )
            {
                continue;
            }

            const std::string key = entry.path().generic_string();
            const auto writeTime = entry.last_write_time( ec );
            if( ec )
            {
                continue;
            }
            seen[key] = writeTime;

            if( InIsInitial )
            {
                continue;
            }
            auto known = m_known.find( key );
            if( known == m_known.end() )
            {
                changes.push_back( { key, ChangeType::Added } );
            }
            else if( known->second != writeTime )
            {
                changes.push_back( { key, ChangeType::Modified } );
            }
        }
    }

    if( !InIsInitial )
    {
        for( const auto& known : m_known )
        {
            if( seen.find( known.first ) == seen.end() )
            {
                changes.push_back( { known.first, ChangeType::Removed } );
            }
        }
    }
    m_known = std::move( seen );

    if( !changes.empty() )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        for( Change& change : changes )
        {
            auto existing = std::find_if( m_pending.begin(), m_pending.end(), [&change]( const Change& pending ) { return pending.FullPath == change.FullPath; } );
            if( existing != m_pending.end() )
            {
                // Added + Modified stays Added; anything + Removed is Removed.
                if( change.Type == ChangeType::Removed || existing->Type != ChangeType::Added )
                {
                    existing->Type = change.Type;
                }
            }
            else
            {
                m_pending.push_back( std::move( change ) );
            }
        }
    }
}


void FileWatcher::ScanNow()
{
    Scan( false );
}


std::vector<FileWatcher::Change> FileWatcher::ConsumeChanges()
{
    std::lock_guard<std::mutex> lock( m_mutex );
    std::vector<Change> changes;
    changes.swap( m_pending );
    return changes;
}


void FileWatcher::ThreadMain()
{
    OPTICK_THREAD( "File Watcher" );
    while( m_running.load() )
    {
        {
            std::unique_lock<std::mutex> lock( m_sleepMutex );
            m_sleepCondition.wait_for( lock, m_interval, [this]() { return !m_running.load(); } );
        }
        if( !m_running.load() )
        {
            break;
        }
        Scan( false );
    }
}
