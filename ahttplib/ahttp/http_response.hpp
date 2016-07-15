/*
This file is part of [ahttp] library. 

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

#ifndef AHTTP_RESPONSE_H
#define AHTTP_RESPONSE_H
#pragma once
#include <boost/utility.hpp>

#include "aconnect/types.hpp"
#include "aconnect/complex_types.hpp"

#include "http_support.hpp"

namespace ahttp
{
	class HttpResponseHeader;
	class HttpResponseStream;
	class HttpResponse;

	class HttpResponseStream : private boost::noncopyable
	{
	public:
		HttpResponseStream  (size_t buffSize, size_t chunkSize) :
			_maxBuffSize (buffSize),
			_maxChunkSize (chunkSize),
			_socket(INVALID_SOCKET),
			_chunked (false),
			_sendContent (true)
		  {};

		  inline void clear ()  {
			  _buffer.clear();
			  _chunked = false;
		  }

		  inline void destroy ()  {
			  clear();
			  _socket = INVALID_SOCKET;
		  }

		  inline void init (aconnect::socket_type sock) {	
			  _socket = sock;
		  };
		  inline bool willBeFlushed (size_t contentSize) const {
			  return ( (_buffer.size() + contentSize) >= _maxBuffSize );
		  }
		  inline size_t getBufferSize() const {
			  return _maxBuffSize;
		  }
		  inline size_t getBufferContentSize() const  {
			  return _buffer.size();
		  }
		  inline aconnect::socket_type socket() const {	
			  return _socket; 
		  }

		  friend class HttpResponse;

	private:
		inline void setChunkedMode () {
			_chunked = true;
		}
		inline bool isChunked () {
			return _chunked;
		}

		inline void setContentWriteFlag (bool writeContent) {
			_sendContent = writeContent;
		}

		void write (string_constref content);
		void write (string_constptr buff, size_t dataSize);
		void flush () throw (aconnect::socket_error);
		void end () throw (aconnect::socket_error);
		void writeDirectly (string_constref content) throw (aconnect::socket_error);
		

	protected:
		size_t _maxBuffSize;
		size_t _maxChunkSize;

		string _buffer;
		aconnect::socket_type _socket;
		bool _chunked;
		bool _sendContent;
	};

	class HttpResponse : private boost::noncopyable
	{

	public:
		HttpResponse (size_t buffSize, size_t chunkSize) :
			Header(),
			Stream (buffSize, chunkSize),
			_clientInfo (NULL),
			_context (NULL),
			_headersSent (false), 
			_finished (false),
			_httpMethod (ahttp::HttpMethod::Unknown)

		{
			
		};

		inline void clear()  
		{
			Header.clear();
			Stream.destroy();
			_clientInfo = NULL;
			_finished = _headersSent = false;
			_serverName.clear();
		}

		inline void init (class HttpContext* context, const aconnect::ClientInfo* clientInfo) 
		{
			assert (context);
			assert (clientInfo);
			
			_context = context;
			_clientInfo = clientInfo;
			Stream.init (clientInfo->sock);
		}

		void sendHeaders () throw (std::runtime_error);
		void sendHeadersContent (string_constptr status, string_constptr headers) throw (std::runtime_error);

		void write (string_constref content);
		void write (string_constptr buff, size_t dataSize);
		void flush () throw (aconnect::socket_error);
		void writeCompleteResponse (string_constref response) throw (std::runtime_error);
		void writeCompleteHtmlResponse (string_constref response) throw (std::runtime_error);

		void end () throw (aconnect::socket_error);

		inline bool isFinished ()			{ return _finished;		};
		inline void setFinished ()			{ _finished = true;		};
		inline bool isHeadersSent ()		{ return _headersSent;	};
		inline bool canSendContent()		{ return _httpMethod != HttpMethod::Head;	};
		
		inline void setServerName (string_constref serverName) {
			_serverName = serverName;
		}
		
		// used to decide how to write content, for HEAD for example
		inline void setHttpMethod (ahttp::HttpMethod::HttpMethodType httpMethod) {
			_httpMethod = httpMethod;
			Stream.setContentWriteFlag (canSendContent());
		}

		
		static string getErrorResponse (int status,
			const HttpServerSettings &settings,
			string_constptr messageFormat = NULL, ...);

	protected:
		void fillCommonResponseHeaders ();
		void applyContentEncoding ();

	// properties
	public:
		HttpResponseHeader						Header;
		HttpResponseStream						Stream;

	protected:	
		const aconnect::ClientInfo*	_clientInfo;
		HttpContext* _context;
		bool _headersSent;
		bool _finished;	
		string _serverName;
		ahttp::HttpMethod::HttpMethodType _httpMethod;
	};

}

#endif // AHTTP_RESPONSE_H

