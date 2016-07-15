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

#include "lib_file_begin.inl"

#include "boost_format_safe.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

#include <iostream>

#include "util.hpp"
#include "util.string.hpp"
#include "util.time.hpp"
#include "logger.hpp"

namespace aconnect
{
	bool Logger::valid()	{	
		return true; 
	}


	void Logger::processMessage (Log::LogLevel level, string_constptr msg)
	{
		if(!valid())
			return;

		if (level > _level)
			return;

		const int bufferSize = 60;
		aconnect::char_type buff[bufferSize] = {0};

		// record example
		// [27-12-2008 01:04:07]   3076 Info: Server started

		struct tm tmTime = util::getDateTime();
		
		string res (msg);
		res.reserve (res.size() + bufferSize);

		string_constptr prefix = NULL;

		switch (level) {
			case Log::Debug:
				prefix = Log::DebugMsg; break;
			case Log::Info:
				prefix = Log::InfoMsg; break;
			case Log::Warning:
				prefix = Log::WarningMsg; break;
			case Log::Error:
				prefix = Log::ErrorMsg; break;
			case Log::Critical:
				prefix = Log::CriticalMsg; break;

			default:
				prefix = Log::UnknownMsg;
		}
		
		int formatted = snprintf (buff, bufferSize, "%02d-%02d-%04d %02d:%02d:%02d (%08X) %s: ", 
			tmTime.tm_mday,
			(tmTime.tm_mon + 1),
			(tmTime.tm_year + 1900),
			tmTime.tm_hour,
			tmTime.tm_min,
			tmTime.tm_sec,

			util::getCurrentThreadId(),
			prefix
		);

		assert (formatted > 0);
		
		res.insert(0, buff, formatted); 
		writeMessage ( res );
	}

	//////////////////////////////////////////////////////////////////////////
	//
	//		ConsoleLogger
	//
	void ConsoleLogger::writeMessage (string_constref msg)
	{
		boost::mutex::scoped_lock lock (_writeMessageMutex);
		std::cout << msg << std::endl;
	}

	//////////////////////////////////////////////////////////////////////////
	//
	//		FileLogger
	//

	bool FileLogger::valid()
	{	
		return _output.is_open() && !_output.fail(); 
	}

	void FileLogger::init (Log::LogLevel level, string_constptr filePathTemplate, 
	        size_t maxFileSize, bool createFile)  throw (std::runtime_error) 
	{
		_level = level;
		_maxFileSize = maxFileSize;

		if (util::isNullOrEmpty (filePathTemplate))
			throw std::runtime_error ("Log file name template is null or empty");

		_filePathTemplate = filePathTemplate;
	
		if (createFile)
    		createLogFile ();
	}

	FileLogger::~FileLogger() {
		destroy();
	}

	void FileLogger::closeWriter () {
		if (valid()) 
			_output.flush();
		_output.rdbuf()->close();
	}

	void FileLogger::destroy() 
	{
		boost::mutex::scoped_lock lock (_writeMessageMutex);
		closeWriter ();
		_output.close();
	}

	string FileLogger::generateTimeStamp() {
		using boost::format;

		struct tm tmTime = util::getDateTime();

		format f("%02d_%02d_%02d_%02d_%02d_%02d");
		f % tmTime.tm_mday % (tmTime.tm_mon + 1) % (tmTime.tm_year + 1900);
		f % (tmTime.tm_hour) % (tmTime.tm_min) % (tmTime.tm_sec);

		return str(f); 
	}


	void FileLogger::createLogFile()  throw (std::runtime_error)
	{
		namespace fs = boost::filesystem;
		using boost::format;

		string ts = generateTimeStamp();
		string fileNameInit;
		if (_filePathTemplate.find(Log::TimeStampMark) != string::npos ) 
			fileNameInit = boost::algorithm::replace_all_copy(_filePathTemplate, Log::TimeStampMark, ts);
		else
			fileNameInit = _filePathTemplate + ts;
		
		fs::path fileName (fileNameInit);
		const string ext = fs::extension (fileName);
		format extFormat (".%06d" + ext);

		int ndx = 0;
		while ( fs::exists (fileName) ) {
			extFormat % ndx;
			fileName = fs::change_extension (fs::path (fileNameInit), 
				extFormat.str() ) ;

			extFormat.clear();
			++ndx;
		}

		closeWriter ();
		_output.open ( fileName.file_string().c_str(), std::ios::out | std::ios::binary );

		if (_output.fail())
			throw std::runtime_error ( str ( format("Cannot create \"%s\" log file") % fileName.file_string().c_str()) );
			
	    _output << "---------------------------------------" << std::endl;
	}

	void FileLogger::writeMessage (string_constref msg)
	{
		boost::mutex::scoped_lock lock (_writeMessageMutex);
		if(!valid())
			return;
		
		writeMessageToFile (msg);
	}

	void FileLogger::writeMessageToFile (string_constref msg) throw (std::runtime_error) 
	{
		_outputSize += msg.size();
		_output << msg << Log::EOL;

		if (_output.fail())
			throw std::runtime_error ("Error writing log file");

		if (_outputSize >= _maxFileSize) {
			createLogFile ();
			_outputSize = 0;
		}

	}

	//////////////////////////////////////////////////////////////////////////
	//
	//		BackgroundFileLogger
	//
	BackgroundFileLogger::BackgroundFileLogger ():
        _writerThread (NULL),
		_messagesBufferSize (0),
		_maxBufferSize (1024 * 1024),
		_flushWaitTimeout (1), // sec
		_active (true)
	{
		
	}

	void BackgroundFileLogger::init (Log::LogLevel level, string_constptr filePathTemplate,
			size_t maxFileSize, bool createFile) throw (std::runtime_error)
	{
	    FileLogger::init (level, filePathTemplate, maxFileSize, false);   
	    
	    _writerThread.reset (new boost::thread (ThreadProcAdapter<void (*)(BackgroundFileLogger*), BackgroundFileLogger*>
				(BackgroundFileLogger::run, this) ));

        boost::thread::yield ();
	}
			
	void BackgroundFileLogger::run (BackgroundFileLogger* backgroundLogger) 
	{
        backgroundLogger->createLogFile ();
        
		while (backgroundLogger->doWaitAndFlush());
	}

	bool BackgroundFileLogger::doWaitAndFlush () 
	{
		// load messages
		boost::try_mutex::scoped_lock lock (_flushMutex);
		
		std::deque<string> messagesToFlush;

		{
			_flushCondition.timed_wait (lock, util::createTimePeriod(_flushWaitTimeout) );
		
			if (!isActive())
				return false;
			
			boost::mutex::scoped_lock writeLock (_writeMessageMutex);
			
			_messages.swap (messagesToFlush);
			_messagesBufferSize = 0;
		}
		    
		writeQueue (messagesToFlush);

		return _active;
	}

	void BackgroundFileLogger::writeMessage(string_constref msg)
	{
		boost::mutex::scoped_lock lock (_writeMessageMutex);
		if(!isActive())
		    return;
		
		_messagesBufferSize += msg.size();
		_messages.push_back(msg);

		if (_messagesBufferSize >= _maxBufferSize)
			_flushCondition.notify_one();
	}
	

	void BackgroundFileLogger::writeQueue (std::deque<string>& messages ) 
	{
		while (messages.size() > 0) 
		{
			writeMessageToFile (messages.front() );
			messages.pop_front();
		}

		_output.flush ();
	}


	void BackgroundFileLogger::destroy()
	{
		{
			boost::try_mutex::scoped_lock lock (_flushMutex);
			_active = false;

            // flush last messages
			writeQueue (_messages);
			
			_flushCondition.notify_one();
		}

		FileLogger::destroy();
	}
	
	//////////////////////////////////////////////////////////////////////////
	//
	//		ProgressTimer
	//

	ProgressTimer::~ProgressTimer () {
		try 
		{
			const int bufferSize = 64;
			aconnect::char_type buff[bufferSize] = {0};

			int formatted = snprintf (buff, bufferSize, 
				"%s: elapsed time - %f sec", 
				_funcName.c_str(),
				_timer.elapsed()
			);
			if (formatted > 0 ) 
			{
				if (formatted == bufferSize)
					buff [bufferSize - 1] = '\0';
				
				_log.processMessage (_level, buff);
			}

		} catch (...) {
			// eat exception
		}
	}

}
