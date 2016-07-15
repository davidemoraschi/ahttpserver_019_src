/*
This file is part of [aconnect] library. 

Author: Artem Kustikov (kustikoff[at]tut.by)
version: 0.19

This code is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any
damages arising from the use of this code.

Permission is granted to anyone to use this code for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this code must not be misrepresented; you must
not claim that you wrote the original code. If you use this
code in a product, an acknowledgment in the product documentation
would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original code.

3. This notice may not be removed or altered from any source
distribution.
*/

#ifndef ACONNECT_LOGGER_H
#define ACONNECT_LOGGER_H

#include <fstream>
#include <boost/timer.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>

namespace aconnect
{
	namespace Log
	{
		enum LogLevel {
			Debug = 3,
			Info = 2,
			Warning = 1,
			Error = 0,
			Critical = -1
		};

		string_constant DebugMsg = "Debug";
		string_constant InfoMsg = "Info";
		string_constant WarningMsg = "Warning";
		string_constant ErrorMsg = "Error";
		string_constant CriticalMsg = "Critical";
		string_constant UnknownMsg = "Unknown";

		string_constant TimeStampMark = "{timestamp}";
		string_constant EOL = "\r\n";

		const size_t MaxFileSize = 4 * 1048576; // 4 Mb
	};

	
	//////////////////////////////////////////////////////////////////////////
	//
	//				abstract class Logger
	//
	//////////////////////////////////////////////////////////////////////////
	class Logger
	{

	protected:
		boost::mutex	_writeMessageMutex;
		Log::LogLevel	_level;
		
		virtual void writeMessage (string_constref msg) = 0;
		virtual bool valid();
		
	public:


		Logger () : _level (Log::Warning) { };
		Logger (Log::LogLevel level) : _level (level) { };
        virtual ~Logger ()  { };
		
		virtual void processMessage (Log::LogLevel level, string_constptr msg);
		
		inline void debug (string_constptr format, ...)	
		{ 
			if (!isDebugEnabled())
				return;
			FORMAT_VA_MESSAGE (format, formattedMessage); 
			processMessage (Log::Debug, formattedMessage.c_str());	
		}

		inline void info  (string_constptr format, ...)	
		{ 
			if (!isInfoEnabled())
				return;
			FORMAT_VA_MESSAGE (format, formattedMessage); processMessage (Log::Info, 
			formattedMessage.c_str());	
		}

		inline void warn  (string_constptr format, ...)	
		{ 
			if (!isWarningEnabled())
				return;
			FORMAT_VA_MESSAGE (format, formattedMessage); 
			processMessage (Log::Warning, formattedMessage.c_str());	
		}

		inline void error (string_constptr format, ...)	
		{ 
			if (!isErrorEnabled()	)
				return;

			FORMAT_VA_MESSAGE (format, formattedMessage); 
			processMessage (Log::Error, formattedMessage.c_str());	
		}

		inline void critical (string_constptr format, ...)	
		{ 
			FORMAT_VA_MESSAGE (format, formattedMessage); 
			processMessage (Log::Critical, formattedMessage.c_str());	
		}

		inline void errorSafe (string_constptr format, ...)	
		{ 
			assert (format);

			string fmt (format);
			if (fmt.find ('%') != string::npos)
				boost::algorithm::replace_all(fmt, "%", "%%");

	
			FORMAT_VA_MESSAGE_DIRECT ( fmt.c_str(), format, formattedMessage); 
			processMessage (Log::Error, formattedMessage.c_str());	
		}
		
		inline void warn (const std::exception &ex)			{	if (isWarningEnabled()) processMessage (Log::Warning, ex.what());	}
		inline void error (const std::exception &ex)		{	if (isErrorEnabled()) processMessage (Log::Error, ex.what());		}
		inline void critical (const std::exception &ex)		{	processMessage (Log::Critical, ex.what());							}
		

		inline bool isDebugEnabled()						{	return _level>=Log::Debug;				}
		inline bool isInfoEnabled()							{	return _level>=Log::Info;				}
		inline bool isWarningEnabled()						{	return _level>=Log::Warning;			}
		inline bool isErrorEnabled()						{	return _level>=Log::Error;				}
	};

	class FakeLogger : public Logger
	{
	public:
		virtual bool valid() {	return false; };
	protected:
		virtual void writeMessage (string_constref msg) {}
	};

	class ConsoleLogger : public Logger
	{
	protected:
		virtual void writeMessage (string_constref msg);
	public:
		ConsoleLogger (Log::LogLevel level) : Logger (level) {
			
		}
	};

	class FileLogger : public Logger
	{
	protected:
		virtual void writeMessage (string_constref msg);
	public:
		
		FileLogger () : _outputSize (0), _maxFileSize(0) {}
		virtual ~FileLogger ();
		
		// filePathTemplate can contain "{timestamp}" mark to replace it with timestamp,
		// if it is omitted then timestamp will be added to the end of file
		virtual void init (Log::LogLevel level, string_constptr filePathTemplate,
			size_t maxFileSize = Log::MaxFileSize, bool createFile = true) throw (std::runtime_error);

		virtual void destroy ();
		virtual bool valid();

	protected:
		void closeWriter ();
		void createLogFile () throw (std::runtime_error);
		string generateTimeStamp();
		void writeMessageToFile (string_constref msg) throw (std::runtime_error);
	
	protected:
		string _filePathTemplate;
		std::ofstream _output;
		size_t _outputSize;
		size_t _maxFileSize;
	};
    
    class BackgroundFileLogger : public FileLogger
	{
	protected:
		std::auto_ptr<boost::thread> _writerThread;
		std::deque<string> _messages;
		size_t _messagesBufferSize;
		size_t _maxBufferSize;
		int _flushWaitTimeout;

		boost::try_mutex _flushMutex;
		boost::condition_variable_any _flushCondition;
		bool _active;


	protected:
		static void run (BackgroundFileLogger* backgroundLogger);
		virtual void writeMessage (string_constref msg);
		bool doWaitAndFlush ();
		void writeQueue (std::deque<string>& messages );


    public:
        BackgroundFileLogger();
        virtual ~BackgroundFileLogger () {}

		virtual void init (Log::LogLevel level, string_constptr filePathTemplate,
			size_t maxFileSize = Log::MaxFileSize, bool createFile = true) throw (std::runtime_error);

		virtual void destroy ();

		inline bool isActive()	{	return _active; }

    };

	class ProgressTimer 
	{
	public:
		ProgressTimer (Logger& log, string_constptr funcName, Log::LogLevel level = Log::Debug):
		  _log (log), _funcName(funcName), _level(level),  _timer() {}
		~ProgressTimer ();

	protected:
		Logger& _log;
		string _funcName;
		Log::LogLevel _level;
		boost::timer _timer;

	};
}


#endif // ACONNECT_LOGGER_H
