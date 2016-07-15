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

#include <cerrno>

#include "util.hpp"
#include "util.time.hpp"
#include "util.network.hpp"
#include "aconnect.hpp"

namespace aconnect 
{
	//////////////////////////////////////////////////////////////////////////
	//
	//		Utility
	class ThreadGuard {
		
	public:
		Server		*server; 

		ThreadGuard (Server	*srv)
			:server (srv) {	assert(server); }
		
		~ThreadGuard () {
			server->removeWorker();
		}
	};

	void WorkerThreadProcAdapter::operator () () {
		
		ThreadGuard guard (_client.server);
	
		try {
			
			do {
				// process request
				_proc (_client);
				
				// guard.server->logDebug("Close socket: %d", _client.sock);
				if (!_client.isClosed())
					_client.close();
					

				
			} while (guard.server->settings().enablePooling 
				&& !guard.server->isStopped()
				&& guard.server->waitRequest (_client));
		
		} catch (server_stopped_error &err) {
			_client.server->logWarning(err);
			_client.server->stop();
		} catch (socket_error &err) {
			_client.server->logWarning(err);
		} catch (std::exception &err) {
			_client.server->logError(err);
		}
	};
	//
	//
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	//
	//		ClientInfo

	ClientInfo::ClientInfo()
	{
		reset();
	}

	void ClientInfo::reset() {
		port = 0; 
		sock = INVALID_SOCKET;
		server = NULL;
		util::zeroMemory(ip, sizeof(ip));
	}

	string ClientInfo::getRequest (SocketStateCheck &stateCheck) const {
		return util::readFromSocket (sock, stateCheck);
	}

	void ClientInfo::writeResponse (string_constref response) const {
		util::writeToSocket (sock, response);
	}

	void ClientInfo::close (bool closeSocket) const throw (socket_error) {
		if (!closeSocket)
			util::closeSocket (this->sock);
		
		this->sock = INVALID_SOCKET;
	}
	
	//
	//
	//////////////////////////////////////////////////////////////////////////
	

	//////////////////////////////////////////////////////////////////////////
	//
	//		Server
	void Server::run (Server *server) 
	{
		socket_type serverSock = server->socket();
		const int maxWorkersCount = server->settings().workersCount;

		try 
		{
       		struct sockaddr_in clientAddr;
            socklen_t clientAddrLen = sizeof (clientAddr);
			
			while (true)
			{
				if (server->isStopped())
					break;

				socket_type clientSock = accept (serverSock, (sockaddr* ) &clientAddr, &clientAddrLen);
				
				if (server->isStopped())
					break;

				try 
				{
				
					if (clientSock == INVALID_SOCKET)
						throw socket_error (clientSock, "Client connection accepting failed");

					// server->logDebug("Socket accepted: %d", clientSock);

					ClientInfo clientInfo;
					clientInfo.sock = clientSock;
					clientInfo.port = clientAddr.sin_port;
					clientInfo.server = server;
					util::readIpAddress (clientInfo.ip, clientAddr.sin_addr);
					
					if (!server->settings().enablePooling) {
						server->workerProc()(clientInfo);
						continue;
					}

					if (server->currentPendingWorkersCount() > 0)  
					{
						boost::mutex::scoped_lock lock (server->pendingMutex());
					
						if (server->currentPendingWorkersCount() > 0) 
						{
							server->requests().push_back (ClientInfo());
							server->requests().back().swap (clientInfo);

							server->pendingCondition().notify_one();
							
							lock.unlock(); // release lock to give time to pending worker
							
							boost::thread::yield ();
							continue;
						}
					}

					// create new worker thread
					{
						boost::mutex::scoped_lock lock (server->loadMutex());
						if (server->currentWorkersCount () < maxWorkersCount) {
							runWorkerThread (server, clientInfo);
							continue;
						}
					}

					
					// wait for worker finish
					boost::mutex::scoped_lock lock (server->finishMutex());
					server->finishCondition().wait (lock);
					runWorkerThread (server, clientInfo);
				
				} catch (socket_error &err) { 

					if (server->isStopped())
						break;

#if defined (__GNUC__)
					if (err.sockerErrorCode() != EAGAIN)
#endif
					{						
						server->logError ("'socket_error' caught at connection accepting: %s", err.what());
					}

				} catch (std::exception &ex)  {

					if (server->isStopped())
						break;

					// thread starting exception, but connection is open:
					//  - server unavailable (boost::thread_resource_error can be caught)
					server->logError ("Exception caught (%s): %s", 
						typeid(ex).name(), ex.what());
					
					if (clientSock != INVALID_SOCKET && 
							server->errorProcessProc())
						server->errorProcessProc()(clientSock);

					try {
						if (clientSock != INVALID_SOCKET)
							util::closeSocket ( clientSock );
					} catch (socket_error &err) {
						server->logWarning ("Client socket closing failed: %s", err.what());
					}   
				} 
				
			}

		} catch (socket_error &err) {
			server->logError ("socket_error caught in server main thread: %s", err.what());
			// close socket
            server->stop();
		
		} catch (...)  {
			server->logError ("Unknown exception caught in main aconnect server thread procedure" );
		} 

		if (server->stopProcessProc())
			server->stopProcessProc()();
	}

	// Start server - it will process request in own thread
	void Server::start (bool inCurrentThread) throw (socket_error)
	{
		boost::mutex::scoped_lock lock (_stopMutex);
		assert (_port != -1);
		assert (_worker_proc);

		_isStopped = false;

		if (!inCurrentThread && NULL != _mainThread)
			throw server_started_error();
	
		_socket = util::createSocket (_settings.domain);
		logDebug ("aconnect server socket created: %d, port: %d", 
			_socket, _port);
	
		applySettings ();

		// bind server
		struct sockaddr_in local;
		util::zeroMemory (&local, sizeof( local ));

		local.sin_family = settings().domain;
		local.sin_port = htons ( port() );
		
		// INADDR_ANY should be set as 0.0.0.0
		util::writeIpAddress (local.sin_addr, _settings.ip);

		char hostname[NI_MAXHOST];
		// Get host name
		int rv = getnameinfo((struct sockaddr *) &local,
							   sizeof (struct sockaddr_in),
							   hostname,
							   NI_MAXHOST, 
							   NULL, 
							   0, 0);

		if (0 == rv) {
			_settings.resolvedHostName = hostname;
		} else {
			logWarning ("Error loading host name, ip: %s", util::formatIpAddr(_settings.ip).c_str());
		}
		
		if ( bind( _socket, (sockaddr*) &local, sizeof(local) ) != 0 )
            throw socket_error (_socket, "Could not bind socket");

        if ( listen( _socket, settings().backlog ) != 0 )
            throw socket_error (_socket, "Listen to socket failed");

		// run thread
		if (inCurrentThread) {
			lock.unlock();
			Server::run (this);
		} else {
			
			_mainThread = new boost::thread (ThreadProcAdapter<server_thread_proc, Server*>
				(Server::run, this) );
			
			boost::thread::yield ();
		}
	}

	void Server::runWorkerThread (Server *server, const ClientInfo &clientInfo) 
	{
		if (server->isStopped())
			return;

		// process request
		WorkerThreadProcAdapter adapter (server->workerProc(), clientInfo);
		server->addWorker();
		
		boost::thread worker (adapter);
		boost::thread::yield ();
	}
	
	bool Server::waitRequest (ClientInfo &client) 
	{
		++_pendingWorkersCount;
		assert (_pendingWorkersCount <= _workersCount 
			&& "Too many pending workers!");

		boost::mutex::scoped_lock lock (_pendingMutex);
		
		// wait for new request
		bool waitResult = _pendingCondition.timed_wait (lock, 
				aconnect::util::createTimePeriod (settings().workerLifeTime) );
		
		--_pendingWorkersCount;
		assert (_pendingWorkersCount >= 0 && "Negative pending workers count!");

		if (waitResult && !isStopped()) 
		{ 
			assert (!requests().empty() && "Requests queue is empty");
			assert (client.isClosed() && "Client connection is not closed correctly");

			client.swap (requests().front());
			requests().pop_front();

			assert (client.sock != INVALID_SOCKET);
		}
		
		return waitResult;
	}
	
	void Server::stop (bool waitAllWorkers)
	{
		boost::mutex::scoped_lock lock (_stopMutex);
		if (_isStopped)
			return;

		_isStopped = true;
		
		if (waitAllWorkers) 
		{
			_logger->debug ("Threads pool unloading, pending workers count: %d", (long) _pendingWorkersCount );

			while (_pendingWorkersCount > 0) 
			{
				boost::mutex::scoped_lock lock (pendingMutex());
				pendingCondition().notify_one();
			}

			_logger->debug ("Threads pool unloading, workers count: %d", (long) _workersCount );

			while (_workersCount > 0) 
			{
				boost::mutex::scoped_lock lock (finishMutex() );
				finishCondition().wait (lock);
			}
		}

		try {
			if (_socket != INVALID_SOCKET) {
				util::closeSocket ( _socket );
				_socket = INVALID_SOCKET;
			}
		} catch (socket_error &err) {
			logWarning (err);
		}

		clear ();      
	}

	

	void Server::applySettings () 
	{
		int intValue = 0;
		
		intValue = (_settings.reuseAddr ? 1 : 0);
		if ( setsockopt( _socket, SOL_SOCKET, SO_REUSEADDR, 
			(char *) &intValue, sizeof(intValue) ) != 0 )
				throw socket_error (_socket, "Reuse address option setup failed");

		util::setSocketReadTimeout ( _socket, _settings.socketReadTimeout );
		util::setSocketWriteTimeout ( _socket, _settings.socketWriteTimeout );
	}
	//
	//
	//////////////////////////////////////////////////////////////////////////
}
