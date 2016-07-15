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

#if defined (WIN32)
#	include <signal.h>
#elif defined (__GNUC__)
#	include <sys/signal.h>
#endif  //__GNUC__

#include <boost/scoped_array.hpp>
#include <boost/algorithm/string.hpp>

#include "util.hpp"
#include "util.network.hpp"
#include "complex_types.hpp"
#include "cstdlib"

namespace algo = boost::algorithm;

namespace aconnect {
	namespace network
	{
		ip_addr_type DefaultLocalIpAddress = {0, 0, 0, 0};
	}

	namespace util {
	
	// create socket with selected domain (Address family) and type
	socket_type createSocket (int domain, int type) throw (socket_error)
	{
		socket_type s = socket (domain, type, 0);
		if (s == INVALID_SOCKET)
			throw socket_error (s, "Socket creation");

		return s;
	}

	void closeSocket (socket_type s, bool throwError) throw (socket_error)
	{
		int res = 0;
#ifdef WIN32
		res = closesocket (s);
#else
		res = close (s);
#endif 
		if (throwError && 0 != res)
			throw socket_error (s, "Socket closing");
	};

	void writeToSocket (socket_type s, string_constref data, bool closeAtError) throw (socket_error)
	{
		if (!data.size())
			return;
		writeToSocket (s, data.c_str(), (int) data.length(), closeAtError);	
	}
	

	void writeToSocket (socket_type s, string_constptr buff, const int buffLen, bool closeAtError) throw (socket_error)
	{
		if (0 == buffLen)
			return;

		string_constptr curPos = buff;
		int bytesCount = buffLen, 
			written = 0;

		do {
			written = send (s, curPos, bytesCount, 0);
			if (written == SOCKET_ERROR) {
				if (closeAtError)
					closeSocket (s, false);

				throw socket_error (s, "Writing data to socket");
			}

			bytesCount -= written;
			curPos += written;

		} while (bytesCount > 0);
	};

	string readFromSocket (socket_type s, 
		SocketStateCheck &stateCheck, 
		bool throwOnConnectionReset,
		const int buffSize) throw (socket_error)
	{
		string data;
		data.reserve (buffSize);

		boost::scoped_array<char_type> buff (new char_type [buffSize]);
		// zeroMemory (buff.get(), buffSize);
		int bytesRead = 0;
		
		stateCheck.prepare (s);

		if (!stateCheck.isDataAvailable (s))
			return data;

		while ( (bytesRead = recv (s, buff.get(), buffSize, 0)) > 0 ) 
		{
			data.append (buff.get(), bytesRead);

			if (stateCheck.readCompleted (s, data))
				break;
		}

		if (bytesRead == SOCKET_ERROR) 
		{
			err_type errCode = socket_error::getSocketError(s);

			if (!throwOnConnectionReset &&
				 (errCode == network::ConnectionAbortCode || errCode == network::ConnectionResetCode)) 
			{
				stateCheck.setConnectionWasClosed (true);
				closeSocket (s);
			
			} else {
				throw socket_error (s, "Reading data from socket");
			}
		}

		return data;
	};

	void setSocketReadTimeout (const socket_type sock, const int timeoutIn /*sec*/)	throw (socket_error)
	{

#if defined (WIN32)
		int timeout = timeoutIn * 1000;
		if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*) &timeout, sizeof(timeout)) == SOCKET_ERROR) 
			throw socket_error (sock, "Socket option SO_RCVTIMEO setup failed");
#else
		timeval sockTimeout;
		sockTimeout.tv_sec = (long) (timeoutIn);	sockTimeout.tv_usec = 0;

		if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void*) &sockTimeout, sizeof(sockTimeout)) == SOCKET_ERROR) 
			throw socket_error (sock, "Socket option SO_RCVTIMEO setup failed");
#endif
	}

	void setSocketWriteTimeout (const socket_type sock, const int timeoutIn /*sec*/)	throw (socket_error)
	{

#if defined (WIN32)
		int timeout = timeoutIn * 1000;
		if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*) &timeout, sizeof(timeout)) == SOCKET_ERROR) 
			throw socket_error (sock, "Socket option SO_SNDTIMEO setup failed");
#else
		timeval sockTimeout;
		sockTimeout.tv_sec = (long) (timeoutIn);	sockTimeout.tv_usec = 0;
		if(setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (void*) &sockTimeout, sizeof(sockTimeout)) == SOCKET_ERROR) 
			throw socket_error (sock, "Socket option SO_SNDTIMEO setup failed");
#endif
	}


	bool checkSocketState (const socket_type sock, const int timeout /*sec*/, bool checkWrite)	throw (socket_error)
	{
		fd_set	sockSet;
		timeval sockTimeout;
		sockTimeout.tv_sec = timeout;
		sockTimeout.tv_usec = 0;

		FD_ZERO ( &sockSet);
		FD_SET (sock, &sockSet);

		int selectRes = 0;
		if (checkWrite)
			selectRes = select ( (int) sock + 1, NULL, &sockSet, NULL, &sockTimeout);
		else
			selectRes = select ( (int) sock + 1, &sockSet, NULL, NULL, &sockTimeout);
			
		if (SOCKET_ERROR == selectRes)	
			throw socket_error (sock, "Checking socket state - 'select' failed");

		if ( 0  == selectRes) // timeout expired
			return false;

		return true; // socket connected
	}


	string formatIpAddr (const ip_addr_type ip) {
		char_type buff[16];
		int cnt = snprintf (buff, 16,
			"%d.%d.%d.%d", 
  			(int) ip[0], (int) ip[1], 
   			(int) ip[2], (int) ip[3]);
		
		return string (buff, cnt);
	}

	bool parseIpAddr (const string str, ip_addr_type& ip)
	{
		// check input
		for (int ndx=0; ndx < (int) str.size(); ++ndx)
			if ( (str[ndx] < '0' || str[ndx] > '9') && str[ndx] != '.')
				return false;

		str_vector parts;
		algo::split (parts, str, algo::is_any_of("."), algo::token_compress_on);
		
		if (parts.size() != 4)
			return false;
		
		for (int ndx=0; ndx<4; ++ndx)
			ip[ndx] =  (byte_type) ( atoi( parts[ndx].c_str() )) % 255; // safe conversion

		return true;
	}

}}
