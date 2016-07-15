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

#ifndef ACONNECT_H
#define ACONNECT_H

#include <boost/utility.hpp>
#include <boost/thread.hpp>
#include <boost/detail/atomic_count.hpp>
#include <list>


#include "types.hpp"
#include "util.hpp"
#include "logger.hpp"
#include "server_settings.hpp"

namespace aconnect 
{
	typedef void (*worker_thread_proc) (const struct ClientInfo&);
	typedef void (*process_error_proc) (const socket_type clientSock);
	typedef void (*server_thread_proc) (class Server *);   
	typedef void (*process_stop_proc) ();
	
	struct ClientInfo
	{
		port_type				port;
		ip_addr_type			ip;
		mutable socket_type		sock;
		class Server			*server;

		// constructor
		ClientInfo();
		
		// methods
		void reset();
  		string getRequest (SocketStateCheck &stateCheck) const;
		void writeResponse (string_constref response) const;
		void close (bool closeSocket = true) const throw (socket_error);
		inline bool isClosed () const { return this->sock == INVALID_SOCKET; }
		
		inline void swap (ClientInfo& other) {
			static const int IpAddrSize = ARRAY_SIZE(ip);

			std::swap (port, other.port);
			std::swap (sock, other.sock);
			
			for (int ndx=0; ndx<IpAddrSize; ++ndx)
				std::swap (ip[ndx], other.ip[ndx]);
			
			std::swap (server, other.server);
		};
	};
	
	struct WorkerThreadProcAdapter
	{
		WorkerThreadProcAdapter (worker_thread_proc proc, ClientInfo client) : // must be copied
			_proc(proc), _client(client) { }
		
		void operator () ();

	private:
        worker_thread_proc  _proc;   
		ClientInfo			_client;
	};
	

	//////////////////////////////////////////////////////////////////////////
	//
	//		Server class
	
	class Server : public IStopable, private boost::noncopyable
	{
    public:
		Server () : 
			_port (-1), 
			_worker_proc (NULL),
			_errorProcess_proc (NULL),
			_stopProcess_proc(NULL),
			_mainThread(NULL), 
			_socket(INVALID_SOCKET),
			_workersCount (0),
			_pendingWorkersCount (0),
            _logger( NULL ),
			_isStopped (false)
		{ }

		virtual ~Server () { 
			clear ();
		}

		void init (port_type port, worker_thread_proc workerProc, 
			const ServerSettings &settings = ServerSettings())
		{
			_port = port;
			_worker_proc = workerProc;
			_settings = settings;
		}

		// Start server - it will process request in own thread
		void start (bool inCurrentThread = false) throw (socket_error);
		void stop (bool waitAllWorkers = false);
		bool waitRequest (ClientInfo &client);

		// server thread function
		void static run (Server *server);

		inline void join ()
        {
			if (_mainThread)
				_mainThread->join();
		}
		

        //////////////////////////////////////////////////////////////////////////////////////////
        //   
        //      Propertie
		virtual bool isStopped ()							{   return _isStopped;      }   

		inline port_type port()	const						{	return _port;		    }
		inline socket_type socket() const					{	return _socket;		    }
		inline const ServerSettings& settings()	const		{	return _settings;	    } 
		inline worker_thread_proc workerProc()const			{	return _worker_proc;     } 
		inline process_error_proc errorProcessProc() const	{	return _errorProcess_proc;} 
		inline process_stop_proc stopProcessProc() const	{	return _stopProcess_proc;} 
		
		inline boost::mutex&  finishMutex()					{	return _finishMutex;	}
		inline boost::condition_variable_any&  finishCondition()			{	return _finishCondition;}
		inline boost::mutex&  pendingMutex()				{	return _pendingMutex;	}
		inline boost::condition_variable_any&  pendingCondition()		{	return _pendingCondition;}
		inline boost::mutex&  stopMutex()					{	return _stopMutex;		}
		inline boost::mutex&  loadMutex()					{	return _loadMutex;		}
		
		inline void setLog (Logger *log)					{   _logger = log;          }
        inline Logger* log () const							{   return _logger;         }   
		
		inline std::list<ClientInfo>& requests()			{	return _requestsQueue;		}
		
		inline void setErrorProcessProc(process_error_proc proc)	{	_errorProcess_proc = proc;} 
		inline void setStopProcessProc(process_stop_proc proc)		{	_stopProcess_proc = proc;} 

        inline void addWorker () {	
			++_workersCount;		
		}

		inline long currentWorkersCount () {	
			return _workersCount;		
		}
		
		inline void removeWorker () {	
			boost::mutex::scoped_lock lock (finishMutex());
			--_workersCount;
            
			assert (_workersCount >= 0 && "Negative workers count!");
			_finishCondition.notify_one();
		}

		inline long currentPendingWorkersCount () {	
			return _pendingWorkersCount;		
		}
			
		inline void logDebug (string_constptr format, ...)	{	
			if (_logger) {
				FORMAT_VA_MESSAGE (format, formattedMessage);
				_logger->debug (formattedMessage.c_str());
			}
		}
		inline void logWarning (string_constptr format, ...)	{	
			if (_logger) {
				FORMAT_VA_MESSAGE (format, formattedMessage);
				_logger->warn (formattedMessage.c_str());
			}
		}
		inline void logWarning (const std::exception &err) {
			if (_logger)
				_logger->warn(err);
		}
		inline void logError( string_constptr format, ...)	{	
			if (_logger) {
				FORMAT_VA_MESSAGE (format, formattedMessage);
				_logger->error (formattedMessage.c_str());
			}
		}
		inline void logError (const std::exception &err) {
			if (_logger)
				_logger->error(err);
		}

	protected:
		void applySettings ();
		void static runWorkerThread (Server *server, const ClientInfo &clientInfo);
		
		inline void clear () {
			if (_mainThread) {
				delete _mainThread; 
				_mainThread = NULL;
			}
		}

	
	// fields
	protected:
		port_type _port;
        worker_thread_proc _worker_proc;
		process_error_proc  _errorProcess_proc;
		process_stop_proc   _stopProcess_proc;

		ServerSettings _settings;
        
        boost::thread *_mainThread;
        socket_type _socket;
        
        boost::detail::atomic_count _workersCount;
		boost::detail::atomic_count _pendingWorkersCount;
        Logger     *_logger;   
		
		boost::mutex _finishMutex;
		boost::condition_variable_any _finishCondition;
		
		boost::mutex _pendingMutex;
		boost::condition_variable_any _pendingCondition;


		boost::mutex _stopMutex;
		boost::mutex _loadMutex;

		bool _isStopped;
		std::list<ClientInfo> _requestsQueue;
	};

	//
	//////////////////////////////////////////////////////////////////////////
}

#endif // ACONNECT_H
