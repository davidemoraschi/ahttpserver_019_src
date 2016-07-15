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

#ifndef AHTTP_SERVER_H
#define AHTTP_SERVER_H
#pragma once
#include <boost/filesystem.hpp>
#include <boost/noncopyable.hpp>
#include <boost/detail/atomic_count.hpp>


#include "aconnect/types.hpp"
#include "aconnect/util.hpp"

#include "ahttp/http_context.hpp"

namespace ahttp
{
	class HttpServer
	{
	private:
		static HttpServerSettings* _globalSettings;
		
	public:
		static HttpServerSettings* GlobalSettings() throw (std::runtime_error) {
			if  (_globalSettings == NULL)
				throw std::runtime_error ("Global HTTP server settings is not loaded");
			if  (_globalSettings->logger() == NULL)
				throw std::runtime_error ("Global HTTP server logger is not initiliazed");

			return _globalSettings;
		}

		inline static string_constref getMessage(string_constref key) 
		{
			return GlobalSettings()->getMessage(key);
		}

		static aconnect::Logger* Log() throw (std::runtime_error) {
			return GlobalSettings()->logger();
		}

		static void init (HttpServerSettings* settings) {
			_globalSettings = settings;
		}

		static boost::detail::atomic_count RequestsCount;

		/**
		* Process HTTP request (and following keep-alive requests on opened socket)
		* @param[in]	client		Filled aconnect::ClientInfo object (with opened socket)
		*/
		static void processConnection (const aconnect::ClientInfo& client);

		/**
		* Process HTTP request - full route
		* @param[in/out]	context		Filled HttpContext instance
		*/
		static bool processRequest (HttpContext& context);

		/**
		* Process worker creation fail (503 HTTP status will be sent)
		* @param[in]	clientSock		opened client socket
		*/
		static void processWorkerCreationError (const aconnect::socket_type clientSock);
		
		static void redirectRequest (HttpContext& context,
			string_constref virtualPath, int status = HttpStatus::Found); 

		/**
		* Send 'Page Not Found' message to client
		* @param[in]	context		current HTTP context
		*/
		static void processError404 (HttpContext& context);

		static void processError403 (HttpContext& context,
			string_constptr message);

		static void processError405 (HttpContext& context,
			string_constref allowedMethods);

		static void processError406 (HttpContext& context, 
			string_constref message);

		/**
		* Send 'Request Entity Too Large' error page to client
		* @param[in]	context		current HTTP context
		*/
		static void processError413 (HttpContext& context);
		
		// send server error response (500+)
		static void processServerError (HttpContext& context, 
			int status = 500, string_constptr message = NULL);

		
	private:
		
		static bool isMethodImplemented (HttpContext& context);
		
		static bool findTarget (HttpContext& context);

		/**
		* Run handlers registered for current directory against current target,
		* returns true if request was completed.
		* @param[in/out]	context		Filled HttpContext instance
		*/
		static bool runHandlers (HttpContext& context, const struct DirectorySettings& dirSettings);
		
		static void applyMappings (HttpContext& context, const struct DirectorySettings& dirSettings);

		static void processDirectFileRequest (HttpContext& context);
		
		static void sendFileToClient (HttpContext& context, 
			std::streamsize fileSize, 
			string_constref filePath, 
			std::time_t modifyTime,
			bool loadRange);

		static void processDirectoryRequest (HttpContext& context, 
			const struct DirectorySettings& dirSettings);

		static string formatParentDirRecord (const DirectorySettings& dirSettings, 
			string_constref parentPath);
		static string formatHeaderRecord (const DirectorySettings& dirSettings, 
			string_constref virtualPath);
		static string formatItemRecord (string_constref itemTemplate,
			string_constref itemUrl,
			string_constref itemName,
			const size_t itemSize,
			std::time_t lastWriteTime);
		static string formatFooterRecord (const DirectorySettings& dirSettings, 
			string_constref virtualPath,
			const size_t fileCount, const size_t dirCount, const size_t errCount);

		inline static string formatDateTime (const struct tm& dateTime)
		{
			const int buffSize = 20;
			aconnect::char_type buff[buffSize] = {0};

			int cnt = snprintf (buff, buffSize, "%.2d.%.2d.%.4d %.2d:%.2d:%.2d", 
				dateTime.tm_mday,
				dateTime.tm_mon + 1,
				dateTime.tm_year + 1900,
				dateTime.tm_hour,
				dateTime.tm_min,
				dateTime.tm_sec
				);

			return string (buff, aconnect::util::min2(cnt, buffSize) );
		}
	};
}
#endif // AHTTP_SERVER_H
