#include "Logger.h"

using namespace MAN;

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

bool Logger::Log(Logger::LogType priority, std::string message)
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

	std::cout << type << message.c_str() << std::endl;
	mLogFile << type << message.c_str() << std::endl;
	mLogFile.close();
	return true;
}