#pragma once
#include <functional>
#include <thread>

class Thread
{
public:
    Thread();

    void Create( std::function<void()> InFunc, const std::string& InName = "" );
    void Join();

    bool IsAlive() const;

protected:
    std::string ThreadName;

private:
    std::thread ThreadFunc;
    bool Kill = false;
};