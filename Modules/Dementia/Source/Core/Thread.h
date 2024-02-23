#pragma once
#include <functional>
#include <thread>
#include <string>

class Thread
{
public:
    Thread();

    void Create( std::function<void()> InFunc, const std::string& InName = "" );
    void Join();
    void SignalShutdown();

    bool IsAlive() const;

protected:
    std::string ThreadName;

private:
    std::thread ThreadFunc;

    // Does this need to be an atomic?
    bool Kill = false;
};