#include "Logger.h"

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

	char* type;
	switch (priority)
	{
	case LogType::Info:
		type = "[Info]: ";
		break;
	case LogType::Trace:
		type = "[Trace]: ";
		break;
	case LogType::Debug:
		type = "[Debug]: ";
		break;
	case LogType::Warning:
		type = "[Warning]: ";
		break;
	case LogType::Error:
		type = "[! Error !]: ";
		break;
	default:
		type = "[Unknown]: ";
		break;
	}

	mLogFile << type << message.c_str() << std::endl;
	std::cout << type << message.c_str() << std::endl;

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
