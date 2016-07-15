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

#include <boost/lexical_cast.hpp>

#include "aconnect/error.hpp"
#include "aconnect/util.string.hpp"


#include "ahttp/http_support.hpp"
#include "ahttp/http_response_header.hpp"

namespace algo = boost::algorithm;

namespace ahttp
{

	string HttpResponseHeader::getContent ()
	{
		using namespace aconnect;
		aconnect::str_stream content;

		content << HttpResponseHeader::getResponseStatusString (Status, _customStatusString);

		for (str2str_map_ci::iterator it = Headers.begin(); it != Headers.end(); it++)
		{
			content << it->first << strings::HeaderValueDelimiter <<
				it->second << strings::HeadersDelimiter;
		}

		content << strings::HeadersDelimiter;
		return content.str();
	}

	void HttpResponseHeader::setContentLength (size_t length) 
	{
		Headers[strings::HeaderContentLength] = boost::lexical_cast<string> (length);
	}
	void HttpResponseHeader::setContentType (string_constref contentType, string_constref charset) {
		if (charset.empty()) 
			Headers[strings::HeaderContentType] = contentType;
		else 
			Headers[strings::HeaderContentType] = contentType + "; charset=" + charset;
	}

	string HttpResponseHeader::getResponseStatusString (int status, string_constref customStatusMsg)
	{
		aconnect::str_stream ret;

		ret << strings::HttpVersion << " " << status
			<< " " << (customStatusMsg.empty() ? strings::httpStatusDesc(status) : customStatusMsg) << strings::HeadersDelimiter;

		return ret.str();
	}

	void HttpResponseHeader::load (string_constptr statusString, string_constptr headerBody) 
		throw (request_processing_error)
	{
		using namespace aconnect;

		string::size_type pos;
		if (statusString)
		{
			_customStatusString = statusString;

			pos = _customStatusString.find(" ");
			
			if (pos != string::npos) {
				Status = boost::lexical_cast<int> (_customStatusString.substr (0, pos));
				_customStatusString.erase (0, pos + 1);
			
			} else {
				Status = boost::lexical_cast<int> (_customStatusString);
			}

			algo::trim (_customStatusString);
		}

		if (aconnect::util::isNullOrEmpty(headerBody))
			return;
		
		str_vector lines;
		algo::split (lines, headerBody, algo::is_any_of("\r\n"), algo::token_compress_on);
		
		for (str_vector::iterator it = lines.begin(); it != lines.end(); ++it) {
			if (it->empty())
				continue;

			pos = it->find (':');
			if (pos == string::npos) 
				throw request_processing_error ("Incorrect reposponse header to send: %s", it->c_str());
				
			Headers [it->substr (0, pos)] = algo::trim_copy(it->substr (pos + 1));
		}
	}
}

