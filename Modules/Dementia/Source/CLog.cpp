#include "CLog.h"
#include "Dementia.h"
#include <string>
#if ME_PLATFORM_WIN64
#include <wtypes.h>
#include <wincon.h>
#include <processenv.h>
#endif

std::vector<CLog::LogEntry> CLog::Messages;

CLog::~CLog()
{
}

void CLog::SetLogFile(std::string filename)
{
	mLogFileLocation = filename;
}

void CLog::SetLogPriority(CLog::LogType priority)
{
	mPriority = priority;
}

bool CLog::LogMessage(CLog::LogType priority, std::string message)
{
	if (mPriority == LogType::None) return false;
	if (priority < mPriority)
		return false;

	mLogFile.open(mLogFileLocation, std::ios_base::app);

	int color = 15;
	std::string type;
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

bool CLog::Log(LogType priority, const std::string& message)
{
	return CLog::GetInstance().LogMessage(priority, message);
}
