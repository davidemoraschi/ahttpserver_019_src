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

#include "aconnect/lib_file_begin.inl"

#include "aconnect/boost_format_safe.hpp"

#include <assert.h>
#include <boost/lexical_cast.hpp>

#include "aconnect/aconnect.hpp"
#include "aconnect/util.hpp"
#include "aconnect/util.time.hpp"

// #include "ahttp/http_support.hpp"
// #include "ahttp/http_server_settings.hpp"
// include "ahttp/http_response_header.hpp"
// #include "ahttp/http_response.hpp"
#include "ahttp/http_context.hpp"

namespace ahttp
{
	//////////////////////////////////////////////////////////////////////////
	//
	//		HttpResponse
	//

	void HttpResponse::fillCommonResponseHeaders () 
	{
		if (!Header.hasHeader (strings::HeaderServer) && !_serverName.empty() )
			Header.Headers [strings::HeaderServer] = _serverName;
		
		if (!Header.hasHeader (strings::HeaderDate))
			Header.Headers [strings::HeaderDate] = formatDate_RFC1123 (aconnect::util::getDateTimeUtc ());

		if ( Header.Status == HttpStatus::OK && !Header.hasHeader (strings::HeaderContentType) )
			Header.setContentType (strings::ContentTypeTextHtml);

		if (!_context->GlobalSettings->isKeepAliveEnabled())
			Header.setConnectionClose();
	}

	void HttpResponse::sendHeaders () throw (std::runtime_error)
	{
		if (_headersSent)
			throw std::runtime_error ("HTTP headers already sent");

		_context->runModules(ModuleCallbackOnResponsePreSendHeaders);
		
		applyContentEncoding();
		fillCommonResponseHeaders();
		
		aconnect::util::writeToSocket (_clientInfo->sock, 
			Header.getContent(), true);
		
		_headersSent = true;

		_context->runModules(ModuleCallbackOnResponsePreSendContent);
	}

	void HttpResponse::sendHeadersContent (string_constptr status, string_constptr headers) throw (std::runtime_error)
	{
		assert ( NULL != status && _httpMethod != ahttp::HttpMethod::Unknown && "HTTP method is not loaded");
		if (_headersSent)
			throw std::runtime_error ("HTTP headers already sent");

		Header.load (status, headers);
		
		sendHeaders ();
	}


	void HttpResponse::applyContentEncoding () 
	{
		if (_finished)
			throw std::runtime_error ("Response already sent");

		if ( !Header.hasHeader (strings::HeaderContentLength) ) 
		{
			Stream.setChunkedMode ();
			Header.Headers[strings::HeaderTransferEncoding] = strings::TransferEncodingChunked;
		}
	}

	void HttpResponse::writeCompleteHtmlResponse (string_constref response) throw (std::runtime_error) 
	{
		Header.setContentType (strings::ContentTypeTextHtml);
		writeCompleteResponse (response);
	}

	void HttpResponse::writeCompleteResponse (string_constref response) throw (std::runtime_error)
	{
		assert ( !_finished && "Response already sent" );
		assert ( !_headersSent && "Headers already sent" );

		if (_headersSent)
			throw std::runtime_error ("HTTP headers already sent");
		if (_finished)
			throw std::runtime_error ("Response already sent");

		
		Header.setContentLength ( response.size ());
		sendHeaders();
	
		Stream.clear();
		Stream.writeDirectly (response);
		
		_finished = true;
	}


	void HttpResponse::write (string_constptr buff, size_t dataSize) 
	{
		if (_finished)
			throw std::runtime_error ("Response already sent");

		if (!_headersSent && Stream.willBeFlushed ( dataSize ))
			sendHeaders();

		Stream.write (buff, dataSize);
	}

	void HttpResponse::write (string_constref content) 
	{
		if (_finished)
			throw std::runtime_error ("Response already sent");

		write (content.c_str(), content.size() );
	}

	void HttpResponse::flush () throw (aconnect::socket_error) 
	{
		if (_finished)
			return;

		if (!_headersSent)
			sendHeaders();
		
		Stream.flush();
	}

	void HttpResponse::end () throw (aconnect::socket_error) 
	{
		if (_finished)
			return;

		// setup correct content length
		if (!_headersSent && !Header.hasHeader (strings::HeaderContentLength)) 
			Header.setContentLength ( Stream.getBufferContentSize() );
		
		flush();
		Stream.end();

		_context->runModules (ModuleCallbackOnResponseEnd); 
		
		_finished = true;
	}

	//////////////////////////////////////////////////////////////////////////
	//
	//		Static helpers
	//
	string HttpResponse::getErrorResponse (int status, 
		const HttpServerSettings &settings,
		string_constptr messageFormat, ...)
	{
		using namespace boost;
		aconnect::str_stream ret;
		string statusDesc = strings::httpStatusDesc (status);
		string description;

		if (messageFormat) {
			FORMAT_VA_MESSAGE (messageFormat, message);
			description.swap (message);
		} else {
			description = settings.getMessage("ErrorUndefined");
		}
		
		ret << str(format( settings.getMessage("MessageFormat") ) 
			% statusDesc % statusDesc % description);
		return ret.str();
	}

	//
	//
	//////////////////////////////////////////////////////////////////////////


	//////////////////////////////////////////////////////////////////////////
	//
	//		HttpResponseStream
	//
	void HttpResponseStream::write (string_constref content) 
	{	
		_buffer.append (content);
		if (_buffer.size() >= _maxBuffSize)
			flush ();
	};

	void HttpResponseStream::write (string_constptr buff, size_t dataSize)
	{	
		_buffer.append (buff, dataSize);
		if (_buffer.size() >= _maxBuffSize)
			flush ();
	};

	void HttpResponseStream::writeDirectly (string_constref content) throw (aconnect::socket_error)
	{
		assert (!_chunked && "writeDirectly must not be called in 'chunked' mode");
		if (_sendContent)
			aconnect::util::writeToSocket (_socket, content, true);
	}

	void HttpResponseStream::flush () throw (aconnect::socket_error)
	{	
		using namespace aconnect;

		if (_buffer.empty())
			return;
		
		if (!_sendContent)
			return;

		if (_chunked) {
			const size_t bufferLen = _buffer.size();
			size_t curPos = 0, chunkSize = bufferLen;
			
			const int chunkLenBufferSize = 8;
			char_type chunkLenBuffer[chunkLenBufferSize] = {0};
			int formatted = 0;

			if (chunkSize > _maxChunkSize)
				chunkSize = _maxChunkSize;
			do 
			{
				// write chunk size
				formatted = snprintf (chunkLenBuffer, chunkLenBufferSize, strings::ChunkHeaderFormat, chunkSize);
				assert (formatted > 0 && "Error formatting chunk size");
				util::writeToSocket (_socket, chunkLenBuffer, formatted);
				
				// write data
				util::writeToSocket (_socket, _buffer.c_str() + curPos, 
					(int) chunkSize);
				
				// write chunk end mark
				util::writeToSocket (_socket, strings::ChunkEndMark);

				curPos += chunkSize;
				chunkSize = util::min2 (_maxChunkSize, bufferLen- curPos);

			} while (curPos < bufferLen);
			
		} else {
			util::writeToSocket (_socket, _buffer);
		}

		_buffer.clear();
	};

	void HttpResponseStream::end () throw (aconnect::socket_error)
	{	
		if (_chunked && _sendContent) {
			// write last chunk
			aconnect::util::writeToSocket (_socket, string (strings::LastChunkFormat) );
		}
	};
}
//
//
//////////////////////////////////////////////////////////////////////////

