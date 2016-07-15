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

#ifndef ACONNECT_ERROR_H
#define ACONNECT_ERROR_H

#include <stdexcept>

#include "types.hpp"

namespace aconnect
{

	struct socket_error : public std::runtime_error 
	{
		socket_error(string_constref msg) : std::runtime_error (msg), _errorCode (0) {	}
		socket_error(socket_type sock, string_constptr msg = NULL) : 
			std::runtime_error (getSocketErrorDesc ( _errorCode = getSocketError(sock), sock, msg ) )	{	}


		static err_type getSocketError (socket_type sock);
		static string getSocketErrorDesc (err_type errCode, socket_type sock, string_constptr msg = NULL);

		inline err_type sockerErrorCode()		{	return _errorCode;	}
	private:
		err_type _errorCode;

	};

	struct server_started_error : public std::runtime_error 
	{
		server_started_error() : std::runtime_error ("Server already started") { }
	};
	struct server_stopped_error : public std::runtime_error 
	{
		server_stopped_error() : std::runtime_error ("Server is stopped") { }
	};
	
	struct thread_interrupted_error : public std::runtime_error 
	{
		thread_interrupted_error() : std::runtime_error ("Thread interrupted") { }
	};

	struct request_processing_error : 
		public std::runtime_error 
	{

			request_processing_error (string_constptr format, ...) : std::runtime_error ("") {
				if (NULL == format) {
					_message = "Request processing failed";
				} else {
					FORMAT_VA_MESSAGE(format, message);
					_message.swap (message);
				}
			}

			virtual ~request_processing_error() throw () { }

			virtual const char * what() const throw () {	
				return (_message.c_str());
			}
	private:
		string _message;
	};


	struct application_error : 
		public std::runtime_error 
	{

		application_error (string_constptr format, ...) : std::runtime_error ("") {
			if (NULL == format) {
				_message = "Request processing failed";
			} else {
				FORMAT_VA_MESSAGE(format, message);
				_message.swap (message);
			}
		}

		virtual ~application_error() throw () { }

		virtual const char * what() const throw () {	
			return (_message.c_str());
		}
	private:
		string _message;
	};
}


#endif // ACONNECT_ERROR_H
