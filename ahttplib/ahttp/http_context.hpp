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

#ifndef AHTTP_CONTEXT_H
#define AHTTP_CONTEXT_H
#pragma once
#include <boost/filesystem.hpp>
#include <boost/noncopyable.hpp>
#include <boost/detail/atomic_count.hpp>


#include "aconnect/types.hpp"
#include "aconnect/util.hpp"

#include "ahttp/aconnect_types.hpp"
#include "ahttp/http_support.hpp"
#include "ahttp/http_server_settings.hpp"
#include "ahttp/http_request.hpp"
#include "ahttp/http_response_header.hpp"
#include "ahttp/http_response.hpp"

namespace ahttp
{
	typedef std::map<string, void*>::iterator internal_item_iterator;
	typedef std::map<string, void*>::const_iterator internal_item_const_iterator;

	class HttpServer;

	struct UploadFileInfo
	{
		string name;
		string fileName;
		string shortFileName;
		string contentType;
		bool isFileData;
		
		size_t fileSize;
		string uploadPath;

		UploadFileInfo () { 
			reset();
		} 

		void loadHeader (string_constref header);
		void reset();
		
	};

	struct request_too_large_error : public aconnect::request_processing_error
	{
		request_too_large_error(size_t sz, size_t maxSize) : 
			aconnect::request_processing_error ("Request size is too large"),
			size (sz),
			maxRequestSize (maxSize)
			{ }

		size_t size;
		size_t maxRequestSize;
	};

	class HttpContext : private boost::noncopyable
	{
	public:	
		HttpContext (const aconnect::ClientInfo* clientInfo, 
			HttpServerSettings* globalSettings,
			aconnect::Logger *log);
		~HttpContext();

		bool init (bool isKeepAliveConnect, 
			long keepAliveTimeoutSec);

		bool isClientConnected() const;
		void closeConnection (bool closeSocket);
		void reset ();
		void setHtmlResponse();

		void parseQueryStringParams ();
		void parseCookies ();
		
		void loadPostParams ();
		string mapPath (string_constptr virtualPath, bool& fileExists) const throw (std::runtime_error);
		
		inline string mapPath (string_constptr virtualPath) const throw (std::runtime_error) {
			bool fileExists =  false;
			return mapPath (virtualPath, fileExists);
		}

		inline void processRequest() 
		{
			parseQueryStringParams ();
			parseCookies();
			loadPostParams ();
		}

		inline bool isClosed() { return Client->isClosed(); } ;
		
		void processException (string_constptr message, bool sendResponse = true, HttpStatus::HttpStatusType status = HttpStatus::InternalServerError);
		
		string getServerVariable (string_constptr variableName);

		bool runModules (ModuleCallbackType callbackType);

		inline void setInternalItem (string_constref key, void* val) {
			InternalItems.insert( std::make_pair (key, val) );
		}

		inline void* getInternalItem (string_constref key, void* defaultValue) const {
			internal_item_const_iterator it = InternalItems.find (key);
			return (it != InternalItems.end() ? it->second : defaultValue);
		}

	protected:
		void loadMultipartFormData (string_constref boundary);
		
		// properties
	public:
		const aconnect::ClientInfo*				Client;

		HttpRequestHeader						RequestHeader;
		HttpRequestStream						RequestStream;
		HttpResponse							Response;

		HttpMethod::HttpMethodType				Method;
		string									InitialVirtualPath;
		string									VirtualPath;
		string									QueryString;
		boost::filesystem::path					FileSystemPath;
		
		HttpServerSettings*						GlobalSettings;
		aconnect::Logger*						Log;	

		boost::filesystem::path					UploadsDirPath;
		
		aconnect::str2str_map					GetParameters;
		aconnect::str2str_map					PostParameters;
		aconnect::str2str_map					Cookies;

		// user-defined data items storage, passed beetwen modules, handlers
		aconnect::str2str_map					Items;
		std::map<string, void*>					InternalItems;

		std::map <string, UploadFileInfo>		UploadedFiles;
		const DirectorySettings*				CurrentDirectoryInfo;
		bool									IsKeepAliveConnect;
		
	};
}
#endif // AHTTP_CONTEXT_H
