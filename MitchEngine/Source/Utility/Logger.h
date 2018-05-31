// 2015 Mitchell Andrews
#pragma once
#include <iostream>
#include <fstream>
#include "Utility/Singleton.h"
/*
Logger.h
A utility class for creating and managing logs for the engine. You can change the
log file name and priority levels to control what info gets saved and where.
*/
namespace MAN
{
	class Logger : public Singleton<Logger>
	{
		friend class Singleton<Logger>;
	public:
		~Logger();
		/*
		The log priority levels.
		Setting a priority will only allow the logger to output
		the set level and higher.
		*/
		enum class LogType : int
		{
			None = 0,
			Info,
			Trace,
			Debug,
			Warning,
			Error
		};

		//************************************
		// Method:    SetLogFile
		// FullName:  Logger::SetLogFile
		// Access:    public
		// Returns:   void
		// Qualifier: Set the log file location to write to.
		// Parameter: std::string filename
		//************************************
		void SetLogFile(std::string filename);
		/*
		Set the logger write priority.
		@param priority: The logger priority to change to
		*/
		void SetLogPriority(Logger::LogType priority);
		/*
		Write a message to the file currently set with the given priority.
		@param priority: The logger priority to change to
		@param message: The message to write to the file
		@returns true if the log was written to the file else false.
		*/
		bool Log(Logger::LogType priority, std::string message);
	private:
		std::ofstream mLogFile;
		std::string mLogFileLocation;
		LogType mPriority;
		Logger() = default;
	};
}