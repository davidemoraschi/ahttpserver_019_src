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

#ifndef ACONNECT_DEFAULTS_H
#define ACONNECT_DEFAULTS_H

#include "util.network.hpp"

namespace aconnect 
{
	// server settings storage - used to setup default server settings
	struct ServerSettings 
	{
		int				backlog;
		int				domain;
		ip_addr_type	ip;				// IP address to bind server socket
		bool			reuseAddr;
		bool			enablePooling;
		int				workersCount;

		int		workerLifeTime;			// sec
		int		socketReadTimeout;		// sec
		int		socketWriteTimeout;		// sec

		string	resolvedHostName;

		// default settings
		ServerSettings () : 
			backlog (SOMAXCONN),	// backlog in listen() call 
			domain (AF_INET),		// domain for 'socket' function call
			reuseAddr (false),		// SO_REUSEADDR flag setup on server socket
			enablePooling (true),	// show whether create worker-threads pool or not
			workersCount (500),		// maximum worker-threads count
			workerLifeTime (300),	// thread in pool lifetime
			socketReadTimeout (60),	// server socket SO_RCVTIMEO timeout
			socketWriteTimeout (60) // server socket SO_SNDTIMEO timeout
		{ 
			memcpy (ip, network::DefaultLocalIpAddress, ARRAY_SIZE(ip) * sizeof(byte_type));
		}
	};
}

#endif // ACONNECT_DEFAULTS_H

