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

#ifndef AHTTP_REQUEST_H
#define AHTTP_REQUEST_H
#pragma once

#include <boost/utility.hpp>

#include "aconnect/types.hpp"
#include "aconnect/complex_types.hpp"

namespace ahttp
{
	class HttpRequestHeader : private boost::noncopyable
	{

	public:
		aconnect::str2str_map_ci Headers;

		int VersionHigh, VersionLow;
		size_t ContentLength;				// Content-Length for POST

		string Method;
		string Path;		// path to source - with query string...

	public:
		HttpRequestHeader () : 
				VersionHigh(0), 
				VersionLow(0), 
				ContentLength (0), 
				_contentLengthLoaded (false)
		{}

		void load (string_constref headerBody) throw (request_processing_error);
		void clear ();
		
		inline bool hasHeader (string_constref headerName) const {
			aconnect::str2str_map_ci::const_iterator iter = Headers.find (headerName);
			return (iter != Headers.end());
		}
		inline string getHeader (string_constref headerName) const {
			aconnect::str2str_map_ci::const_iterator iter = Headers.find (headerName);
			if (iter == Headers.end())
				return "";
			return iter->second;
		}

		inline bool removeHeader (string_constref headerName)  {
			aconnect::str2str_map_ci::iterator iter = Headers.find (headerName);
			if (iter == Headers.end())
				return false;

			Headers.erase (iter);
			return true;
		}

		inline string operator[] (string_constref headerName) const {
			return getHeader (headerName);
		}
		
		inline bool isContentLengthRead() const  {
			return _contentLengthLoaded;
		}
        
	protected:
		void loadHeader (string_constref name, string_constref value);

		bool _contentLengthLoaded;
	};


	class HttpRequestStream : private boost::noncopyable
	{
	public:
		HttpRequestStream () : 
			ContentLength(0), 
			_socket (INVALID_SOCKET),
			_loadedContentLength (0)	
			{};
		
		void init (string& requestBodyBegin, int contentLength, aconnect::socket_type socket);
		int read (string_ptr buff, int buffSize) throw (aconnect::socket_error);
		
		inline void clear() {
			ContentLength = 0;
			_requestBodyBegin.clear();
		}

		inline bool hasBufferedContent() const			{	return !_requestBodyBegin.empty();	}
		inline size_t getBufferedContentLength() const	{	return _requestBodyBegin.size();	}
		inline size_t getLoadedContentLength() const	{	return _loadedContentLength;		}
		
		inline void giveBuffer(string& dest)	
		{	
			dest.swap(_requestBodyBegin);
			_loadedContentLength += dest.size();
		}
		
		inline bool isRead() const					{	return _loadedContentLength == ContentLength; }
		inline aconnect::socket_type socket() const	{	return _socket; }

	public:
		int ContentLength;

	protected:
		string _requestBodyBegin;
		aconnect::socket_type _socket;
		int _loadedContentLength;

	};
}
#endif // AHTTP_REQUEST_H

