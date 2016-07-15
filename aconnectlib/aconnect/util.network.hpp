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

#ifndef ACONNECT_NETWORK_UTIL_H
#define ACONNECT_NETWORK_UTIL_H

// socket related functionality
#ifdef WIN32
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <process.h>

#	pragma comment (lib, "ws2_32.lib")
#endif  //WIN32

#ifdef __GNUC__
#	include <fcntl.h> 
#	include <sys/types.h>
#	include <sys/stat.h> 
#	include <unistd.h>
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#	include <netdb.h>
#endif  //__GNUC__

#include <cerrno>
#include <boost/algorithm/string.hpp>

#include "types.hpp"
#include "error.hpp"

namespace aconnect 
{
	struct socket_error;
	class SocketStateCheck;

	namespace network
	{
		extern ip_addr_type DefaultLocalIpAddress;

		const int SocketReadBufferSize = 512*1024; // bytes
#if defined (WIN32)
		const err_type ConnectionAbortCode = WSAECONNABORTED;
		const err_type ConnectionResetCode = WSAECONNRESET;
#else
		const err_type ConnectionAbortCode = ECONNABORTED;
		const err_type ConnectionResetCode = ECONNRESET;
#endif

		string_constant LocalhostName	=	"localhost";
		const ip_addr_type LocalhostAddress = {127, 0, 0, 1};
	}


	namespace util 
	{

		// sockets support
		/*
		* create socket with selected domain (Address family) and type
		*/
		socket_type createSocket (int domain = AF_INET, int type = SOCK_STREAM) throw (socket_error);


		/*
		*	Close socket
		*	@param[in]	sock		Opened client socket
		*	@param[in]	throwError	Define wheter to throw exception when socket is not closed succesfully (e.g. already closed)
		*/
		void closeSocket (socket_type s, bool throwError = true) throw (socket_error);

		/*
		*	Write content of 'data' to socket
		*	@param[in]	sock		Opened client socket
		*	@param[in]	data		Data to write (can contain '\0')
		*/
		void writeToSocket (socket_type sock, string_constref data, bool closeAtError = false) throw (socket_error);
		void writeToSocket (socket_type s, string_constptr buff, const int buffLen, bool closeAtError = false) throw (socket_error);
		string readFromSocket (const socket_type s, SocketStateCheck &stateCheck, bool throwOnConnectionReset = true, 
				const int buffSize = network::SocketReadBufferSize) throw (socket_error);
		
		inline void readIpAddress (ip_addr_type ip, const in_addr &addr) {
#ifdef WIN32
			ip[0] = addr.s_net;
			ip[1] = addr.s_host;
			ip[2] = addr.s_lh;
			ip[3] = addr.s_impno;
#else
			ip[0] = (byte_type) (addr.s_addr >> 24);
			ip[1] = (byte_type) (addr.s_addr >> 16) % 255;
			ip[2] = (byte_type) (addr.s_addr >> 8) % 255;
			ip[3] = (byte_type) addr.s_addr % 255;
	        
#endif 
		};

		inline void writeIpAddress (in_addr &addr, const ip_addr_type ip) {
#ifdef WIN32
			addr.S_un.S_addr = (ip[3] << 24) + (ip[2] << 16) + (ip[1] << 8) + ip[0];
#else
			addr.s_addr = (ip[3] << 24) + (ip[2] << 16) + (ip[1] << 8) + ip[0];    
#endif 
		};

		string formatIpAddr (const ip_addr_type ip);
		/*
		*	Try to parse IP address from n.n.n.n form
		*	@param[in]	str		String IP represenation
		*	@param[out]	ip		Storage to load
		*/
		bool parseIpAddr (const string str, ip_addr_type& ip);

		inline bool isLocalhostIpAddress (const ip_addr_type ip) {
			
			if ( 0 == ip[0] && 0 == ip[1] && 0 == ip[2] && 0 == ip[3]) return true;
			if ( 127 == ip[0] && 0 == ip[1] && 0 == ip[2] && 1 == ip[3]) return true;

			return false;
		};

		void setSocketReadTimeout (const socket_type sock, const int timeoutIn /*sec*/)	throw (socket_error);
		void setSocketWriteTimeout (const socket_type sock, const int timeoutIn /*sec*/)	throw (socket_error);

		bool checkSocketState (const socket_type sock, const int timeout /*sec*/, bool checkWrite = false)	throw (socket_error);
	}

	class Initializer 
	{
	public:
		Initializer (bool initIt = true) {
			if (initIt)
				init ();
		}
		~Initializer () {
			destroy ();
		}        

		static void init () {
#ifdef WIN32
			WSADATA wsaData;
			int errCode = WSAStartup( MAKEWORD( 2, 2 ), &wsaData );
			if ( errCode != 0 ) 
				throw socket_error ( socket_error::getSocketErrorDesc(errCode, INVALID_SOCKET, "WSAStartup failed") );
#endif
		};
		static void destroy () {
#ifdef WIN32
			if (WSACleanup() == SOCKET_ERROR)	
				throw socket_error ( "WSACleanup failed");
#endif
		};
	};

	// Socket state checkers (check read/write availability)
	class SocketStateCheck {
		public:
			SocketStateCheck () : _connectionWasClosed (false) { };
			virtual  ~SocketStateCheck() {};
			virtual void prepare (socket_type s) {	};
			virtual bool readCompleted (socket_type s, string_constref data) = 0;
			virtual bool isDataAvailable (socket_type s) { return true; }; // default behavior

			inline void setConnectionWasClosed (bool closed)	{	_connectionWasClosed = closed;	}
			inline bool connectionWasClosed() const				{	return _connectionWasClosed;	}

	protected:
		bool				_connectionWasClosed;
	};
	
	class FastSelectReadSocketStateCheck : public SocketStateCheck
	{
		protected:
			fd_set	_set;
			timeval _readTimeout;

		public:
			FastSelectReadSocketStateCheck (long timeoutSec = 0) {
				_readTimeout.tv_sec = timeoutSec;
				_readTimeout.tv_usec = 0;
			}

			virtual void prepare (socket_type s) {
				FD_ZERO ( &_set);
				FD_SET (s, &_set);
			};

			virtual bool isDataAvailable (socket_type s) {
				int selectRes = select ( (int) s + 1, &_set, NULL, NULL, &_readTimeout);
			
				if (SOCKET_ERROR == selectRes)	
					throw socket_error (s, "Reading data from socket: select failed");

				if ( 0  == selectRes) // timeout expired
					return false;
			
				return true; // there is data to read
			}

			virtual bool readCompleted (socket_type s, string_constref data) {
				return !isDataAvailable(s); 
			}

	};

	class EndMarkSocketStateCheck : public SocketStateCheck
	{
		public:
			EndMarkSocketStateCheck (string_constref endMark = "\r\n\r\n") : _endMark(endMark) {	}

			virtual bool readCompleted (socket_type s, string_constref data) {
				return boost::algorithm::ends_with (data, _endMark);
			}
			
			inline const string& endMark() const {	return _endMark; }  
	protected:
		string _endMark;
	};
	

}

#endif // ACONNECT_NETWORK_UTIL_H

