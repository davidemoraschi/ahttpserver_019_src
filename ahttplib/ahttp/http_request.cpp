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

#include <assert.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include "aconnect/util.hpp"
#include "aconnect/util.string.hpp"
#include "aconnect/util.network.hpp"

#include "ahttp/http_support.hpp"
#include "ahttp/http_request.hpp"

namespace algo = boost::algorithm;

namespace ahttp
{
	void HttpRequestHeader::clear()
	{
		Headers.clear ();

		VersionHigh = VersionLow = 0;
		ContentLength = 0;
		_contentLengthLoaded = false;

		Method.clear ();
		Path.clear ();
	}

	void HttpRequestHeader::load (string_constref headerBody) 
		throw (request_processing_error)
	{
		assert ( headerBody.length() );

		using namespace aconnect;
				
		SimpleTokenizer tokens(headerBody, "\r\n");
		
		int lineNdx = 0;
		string::size_type pos = string::npos, 
			prevPos = string::npos;

		for (SimpleTokenizer::iterator tok_iter = tokens.begin(); 
				tok_iter != tokens.end(); 
				++tok_iter)
		{
			string_constref line = tok_iter.current_token();
			if (line.empty())
				continue;

			// first line: "GET /preloadingpages HTTP/1.1"
			if (0 == lineNdx) {
				pos = line.find (' ');
				if (pos == string::npos) 
					throw request_processing_error ("Incorrect request string: %s", line.c_str());
				
				Method = line.substr (0, pos);
				prevPos = pos + 1;

				pos = line.find (' ', pos + 1);
				
				if (pos != string::npos) {
					Path = line.substr (prevPos, pos - prevPos);
					prevPos = pos + 1;

					const size_t offset = strlen ("HTTP/");

					if ((pos = line.find ('.', prevPos + offset)) != string::npos) {
						VersionHigh = boost::lexical_cast<int> (line.substr (prevPos + offset, pos - offset - prevPos));
						VersionLow = boost::lexical_cast<int> (line.substr (pos + 1));

					} else {
						VersionHigh = boost::lexical_cast<int> (line.substr (prevPos + offset));
						VersionLow = 0;
					}

				} else {
					Path = line.substr (prevPos);
				}

			} else {
				pos = line.find (':');
				if (pos == string::npos) 
					throw request_processing_error ("Incorrect request header: %s", line.c_str());
				
				loadHeader (line.substr (0, pos), algo::trim_copy(line.substr (pos + 1)));
			}

			++lineNdx;
		}

		
		// INVESTIGATE: check correctness, maybe DEBUG, OPTIONS... will be supported
		if (util::equals (Method, strings::HttpMethodGet))
			_contentLengthLoaded = true;

	}

	void HttpRequestHeader::loadHeader (string_constref name, string_constref value) 
	{
		using namespace aconnect;
		
		if ( util::equals (name, strings::HeaderContentLength) ) {
			ContentLength = boost::lexical_cast<size_t> (value);
			_contentLengthLoaded = true;
		}
		
		Headers.insert(std::make_pair (name, value));
	}

	//////////////////////////////////////////////////////////////////////////
	//
	//		HttpRequestStream
	//
	//////////////////////////////////////////////////////////////////////////

	void HttpRequestStream::init (string& requestBodyBegin, 
								 int contentLength, aconnect::socket_type sock) 
	{
		ContentLength = contentLength;
		_socket = sock;
		_loadedContentLength = 0;

		if (contentLength > 0) 
			_requestBodyBegin.swap (requestBodyBegin);
	}

	int HttpRequestStream::read (string_ptr buff, int buffSize) throw (aconnect::socket_error)
	{
		if (0 == ContentLength)
			return 0;

		// read from buffer
		if (!_requestBodyBegin.empty()) {
			int copied = (int) _requestBodyBegin.copy(buff, aconnect::util::min2 ( buffSize, 
				(int) _requestBodyBegin.length()));

			_requestBodyBegin.erase(0, copied);
			_loadedContentLength += copied;

			return copied;
		}

		if (_loadedContentLength == ContentLength)
			return 0;

		// read from socket
		if (buffSize > (ContentLength - _loadedContentLength))
			buffSize = ContentLength - _loadedContentLength;

		int bytesRead = recv (_socket, buff, buffSize, 0);
		if (bytesRead == SOCKET_ERROR)
			throw aconnect::socket_error (_socket, "HTTP request: reading data from socket failed");

		_loadedContentLength += bytesRead;

		return bytesRead;
	}
}
