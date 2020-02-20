#include "Logger.h"
#include <wtypes.h>
#include <wincon.h>
#include <processenv.h>

std::vector<Logger::LogEntry> Logger::Messages;

Logger::~Logger()
{
}

void Logger::SetLogFile(std::string filename)
{
	mLogFileLocation = filename;
}

void Logger::SetLogPriority(Logger::LogType priority)
{
	mPriority = priority;
}

bool Logger::LogMessage(Logger::LogType priority, std::string message)
{
	if (mPriority == LogType::None) return false;
	if (priority < mPriority)
		return false;

	mLogFile.open(mLogFileLocation, std::ios_base::app);

	int color = 15;
	char* type;
	switch (priority)
	{
	case LogType::Info:
		type = "[Info]: ";
		color = 8;
		break;
	case LogType::Trace:
		type = "[Trace]: ";
		break;
	case LogType::Debug:
		type = "[Debug]: ";
		break;
	case LogType::Warning:
		type = "[Warning]: ";
		color = 14;
		break;
	case LogType::Error:
		type = "[! Error !]: ";
		color = 12;
		break;
	default:
		type = "[Unknown]: ";
		break;
	}

#if ME_PLATFORM_WIN64
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hConsole, color);

	mLogFile << type << message.c_str() << std::endl;
	std::cout << type << message.c_str() << std::endl;

	SetConsoleTextAttribute(hConsole, 15);
#endif

#if ME_EDITOR
	Messages.emplace_back(LogEntry{ priority, std::move(message) });
#else
#endif

	mLogFile.close();
	return true;
}

bool Logger::Log(LogType priority, const std::string& message)
{
	return Logger::GetInstance().LogMessage(priority, message);
}
