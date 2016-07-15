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

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>

#include <assert.h>

#include "aconnect/util.string.hpp"
#include "aconnect/util.time.hpp"
#include "aconnect/util.file.hpp"
#include "aconnect/complex_types.hpp"
#include "aconnect/error.hpp"

#include "ahttplib.hpp"


namespace fs = boost::filesystem;
namespace algo = boost::algorithm;

namespace ahttp 
{
	HttpServerSettings* HttpServer::_globalSettings = NULL;
	boost::detail::atomic_count HttpServer::RequestsCount (0);

	//////////////////////////////////////////////////////////////////////////
	//
	// Process worker thread creation fail
	//
	//
	void HttpServer::processWorkerCreationError (const aconnect::socket_type clientSock) 
	{
		using namespace aconnect;
		
		string content = HttpResponse::getErrorResponse(503,
			*GlobalSettings(),
			getMessage("Error503").c_str());

		str_stream response;

		// create header
		response << HttpResponseHeader::getResponseStatusString (503);
		response << strings::HeaderContentType << strings::HeaderValueDelimiter << strings::ContentTypeTextHtml << strings::HeadersDelimiter;
		response << strings::HeaderContentLength << strings::HeaderValueDelimiter << content.length() << strings::HeadersDelimiter;
		response << strings::HeaderServer << strings::HeaderValueDelimiter << GlobalSettings()->serverVersion() << strings::HeadersDelimiter;

		aconnect::util::writeToSocket (clientSock, response.str(), true);
	}

	// check HTTP method availability - sent 501 on fail
	bool HttpServer::isMethodImplemented (HttpContext& context)
	{
		using namespace aconnect;
		string_constref method = context.RequestHeader.Method;

		if (method.empty()) {
			Log()->warn("Empty HTTP method retrieved in request");
			return false;
		}

		if (util::equals(method, strings::HttpMethodGet)) {
			context.Method = HttpMethod::Get;
			return true;
		}
		if (util::equals(method, strings::HttpMethodPost)) {
			context.Method = HttpMethod::Post;
			return true;
		}
		if (util::equals(method, strings::HttpMethodHead)) {
			context.Method = HttpMethod::Head;
			return true;
		}

		// format "Not Implemented" response
		context.Response.Header.Status = 501;
		string errorResponse = HttpResponse::getErrorResponse (context.Response.Header.Status,
			*GlobalSettings(),
			getMessage("Error501_MethodNotImplemented").c_str(), 
			method.c_str());

		context.Response.Header.setContentType (strings::ContentTypeTextHtml);
		context.Response.writeCompleteResponse (errorResponse);
		
		return false;
	}

	void HttpServer::redirectRequest (HttpContext& context,
								string_constref virtualPath, 
								int status)
	{
		using namespace aconnect;

		context.Response.Header.Status = status;
		context.Response.Header.Headers [strings::HeaderLocation] = virtualPath;

		string errorResponse = HttpResponse::getErrorResponse (context.Response.Header.Status,
			*GlobalSettings(),
			getMessage("ErrorDocumentMoved").c_str(), 
			virtualPath.c_str() );
		
		context.Response.writeCompleteHtmlResponse (errorResponse);
	}

	// format and send 403 error
	void HttpServer::processError403 (HttpContext& context,
								 string_constptr message)
	{
		using namespace aconnect;
		
		context.Response.Header.Status = 403;
		string errorResponse = HttpResponse::getErrorResponse (context.Response.Header.Status,
			*GlobalSettings(),
			message);
		context.Response.writeCompleteHtmlResponse (errorResponse);
	}

	// format and send 404 error
	void HttpServer::processError404 (HttpContext& context)
	{
		using namespace aconnect;

		// format "Not Found" response
		context.Response.Header.Status = 404;
		string errorResponse = HttpResponse::getErrorResponse (context.Response.Header.Status,
			*GlobalSettings(),
			getMessage("Error404").c_str(), 
			context.InitialVirtualPath.c_str());
		
		context.Response.writeCompleteHtmlResponse (errorResponse);
	}

	void HttpServer::processError405 (HttpContext& context,
									  string_constref allowedMethods) 
	{

		using namespace aconnect;

		// format "Method Not Allowed" response
		context.Response.Header.Status = 405;
		string errorResponse = HttpResponse::getErrorResponse (context.Response.Header.Status,
			*GlobalSettings(),
			getMessage("Error405").c_str(), 
			context.RequestHeader.Method.c_str(), allowedMethods.c_str());

		context.Response.Header.Headers[strings::HeaderAllow] = allowedMethods;
		context.Response.Header.Headers[strings::HeaderConnection] = strings::ConnectionClose;
		context.Response.writeCompleteHtmlResponse (errorResponse);
	}

	void HttpServer::processError406 (HttpContext& context, 
		string_constref message) 
	{
		// format "Not Acceptable" response
		context.Response.Header.Status = HttpStatus::NotAcceptable;
		string errorResponse = HttpResponse::getErrorResponse (context.Response.Header.Status,
			*GlobalSettings(),
			message.c_str());
		context.Response.writeCompleteHtmlResponse (errorResponse);
	}

	// format and send 413 error
	void HttpServer::processError413 (HttpContext& context)
	{
		using namespace aconnect;
		
		context.Response.Header.Status = HttpStatus::RequestEntityTooLarge;
		string errorResponse = HttpResponse::getErrorResponse (context.Response.Header.Status,
			*GlobalSettings(), 
			getMessage("Error413").c_str());
		
		context.Response.Header.Headers[strings::HeaderConnection] = strings::ConnectionClose;
		context.Response.writeCompleteHtmlResponse (errorResponse);
	}

	// send server error response (500+)
	void HttpServer::processServerError (HttpContext& context,
								 int status, string_constptr message)
	{
		using namespace aconnect;

		try
		{
			if (!context.isClientConnected())
			{
				context.Log->error(
					boost::str(boost::format ("Server error: %s %s") % 
						strings::httpStatusDesc (status) % (message ? message : "")).c_str() );
			}


			if (!context.Response.isHeadersSent()) 
			{
				string errorResponse = HttpResponse::getErrorResponse (status,
					*GlobalSettings(),
					getMessage("Error500").c_str(), 
					(message ? message : "") );

				context.Response.Header.Status = status;
				context.Response.Header.Headers[strings::HeaderConnection] = strings::ConnectionClose;
				context.Response.writeCompleteHtmlResponse (errorResponse);
			
			} else if (!context.Response.isFinished()) {

				string response = 
					boost::str(boost::format (getMessage("MessageFormatInline")) % 
						strings::httpStatusDesc (status) % (message ? message : ""));
				
				context.Response.write (response);
				context.Response.end();
			}
			
			context.closeConnection(true);
			
		} catch (...)  {
			context.processException ("Server error info sending failed", false);
		}
	}

	//////////////////////////////////////////////////////////////////////////
	//		HTTP request processing procedure
	//////////////////////////////////////////////////////////////////////////

	void HttpServer::processConnection (const aconnect::ClientInfo& client)
	{
		using namespace aconnect;
		string connectionHeader, requestString;
		try
		{
			bool isKeepAliveConnect = false;

			do {
				requestString.clear();

				std::auto_ptr<HttpContext> context( new HttpContext (
					&client, 
					HttpServer::GlobalSettings(),
					HttpServer::GlobalSettings()->logger()));

				bool loaded = context->init (isKeepAliveConnect, 
					GlobalSettings()->keepAliveTimeout());

				if (!loaded)
					break;

				requestString = context->RequestHeader.Path;

				if (processRequest (*context))
					break;
				
				if (context->isClosed())
					break;

				if (!GlobalSettings()->isKeepAliveEnabled())
					break;

				connectionHeader = context->getServerVariable (strings::ServerVariables::HTTP_CONNECTION);
				
				if (util::equals (context->Response.Header.Headers[strings::HeaderConnection], 
						strings::ConnectionClose))
					isKeepAliveConnect = false;
				else
					isKeepAliveConnect = util::equals (connectionHeader, strings::ConnectionKeepAlive);
			
			// process subsequent "Keep-Alive" requests
			} while ( isKeepAliveConnect );

		} catch (socket_error &ex)  {
			Log()->warn ("[<=] socket_error caught (%s): %s, client IP: %s, path: %s", 
				typeid(ex).name(), ex.what(), 
				util::formatIpAddr (client.ip).c_str(),
				requestString.empty() ? "<not loaded>" : requestString.c_str());

		} catch (std::exception &ex)  {
			Log()->error ("[<=] Exception caught (%s): %s, client IP: %s, path: %s", 
				typeid(ex).name(), ex.what(), 
				util::formatIpAddr (client.ip).c_str(),
				requestString.empty() ? "<not loaded>" : requestString.c_str());

		} catch (...)  {
			Log()->error ("[<=] Unknown exception caught, client IP: %s", 
				util::formatIpAddr (client.ip).c_str() );
		}

	}

	bool HttpServer::processRequest (HttpContext &context)
	{
		using namespace aconnect;
		boost::timer requestTimer;

		++RequestsCount;

		if ( context.runModules(ModuleCallbackOnRequestBegin) )
			return true;
		
		if (!isMethodImplemented (context))
			return true;

		context.Response.setHttpMethod (context.Method);

		context.VirtualPath = 
			context.InitialVirtualPath = context.RequestHeader.Path.substr(0, context.RequestHeader.Path.find("?"));

		if (context.RequestHeader.Path.length() > context.VirtualPath.length())
			context.QueryString = context.RequestHeader.Path.substr (context.InitialVirtualPath.size() + 1);

		bool stopKeepAlive = false;
        
		try {

			// find request target by URL and process it when it is a real file
			bool processFile = findTarget (context);
			if (processFile)
				processDirectFileRequest (context);

		} catch (request_too_large_error &rlex)  {
						
			Log()->warn ("Request size is too large - %d, allowed size: %d, target: %s", 
				rlex.size,
				rlex.maxRequestSize,
				context.VirtualPath.c_str());

			processError413 (context);
			context.closeConnection (true);

			stopKeepAlive = true;

		// try to send 500 error
		} catch (request_processing_error &ex) {
			Log()->error (ex);
			processServerError(context, ahttp::HttpStatus::InternalServerError, ex.what());
            
			stopKeepAlive = true;
		}
                
		if (context.isClosed()) {
			stopKeepAlive =  true;
		
		// check request state - it must be read at this point
		} else if ( !context.RequestStream.isRead()) {
			context.loadPostParams();

			processServerError(context, 
				ahttp::HttpStatus::InternalServerError,
				getMessage("Error500_RequestNotLoaded").c_str());
			
            stopKeepAlive = true;

       } else if (!context.Response.isFinished ()) {
			// check response correctness
            
            if (context.Response.Header.Status == HttpResponseHeader::UnknownStatus) {
				processServerError(context, 
					ahttp::HttpStatus::InternalServerError,
					boost::str(boost::format (getMessage("Error500_ProcessingFailed")) % context.InitialVirtualPath).c_str());
                    
                stopKeepAlive = true;
			} else {
				context.Response.end();
            }
		}
        
        if ( Log()->isInfoEnabled() )
			Log()->info ("[=>] %s\t%d\t%s\t%f\t%s\t", 
                context.RequestHeader.Method.c_str(), 
                context.Response.Header.Status,
				util::formatIpAddr (context.Client->ip).c_str(),
				requestTimer.elapsed(),
                context.RequestHeader.Path.c_str());

	
		return stopKeepAlive;
	}

	void HttpServer::applyMappings (HttpContext& context, 
		const struct DirectorySettings& dirSettings)
	{
		using namespace aconnect;

		if (!dirSettings.mappings.empty ()) {
			
			string virtualPath = context.InitialVirtualPath.substr (dirSettings.virtualPath.length());
            boost::smatch matches;
			string val;
                
			for (mappings_vector::const_iterator iter = dirSettings.mappings.begin();
					iter != dirSettings.mappings.end();
					++iter) 
			{
				if (boost::regex_match (virtualPath, matches, boost::regex(iter->first) )) {
					
					string target = iter->second;
					
					for (int ndx = 1; ndx< (int) matches.size(); ++ndx) 
					{
						if (matches[ndx].matched) 
							val = matches.str(ndx);
                        else
                            val.clear();
						
                        // skip "{{" and "}}"
						boost::regex re ( "(^|[^\\{])\\{" + boost::lexical_cast<string> (ndx - 1) + "\\}([^\\}]|$)"); 
						target = boost::regex_replace (target, re, "($1)" + matches.str(ndx) + "($2)", 
							boost::match_default | boost::format_all );

						// More simple variant:
						// boost::regex re ( "\\{" + boost::lexical_cast<string> (ndx - 1) + "\\}"); 
						// target = boost::regex_replace (target, re, matches.str(ndx));
					}

					// update path and query string
					context.RequestHeader.Path = dirSettings.virtualPath + target;
					context.VirtualPath = context.RequestHeader.Path.substr(0, context.RequestHeader.Path.find("?"));
					if (context.RequestHeader.Path.length() > context.VirtualPath.length())
						context.QueryString = context.RequestHeader.Path.substr (context.VirtualPath.size() + 1);
				}
			}
		}


	}


	bool HttpServer::findTarget (HttpContext& context) 
	{
		using namespace aconnect;
		const directories_map &directories = GlobalSettings()->Directories();
		directories_map::const_iterator rootRecord = directories.find (strings::Slash);
 
		if (rootRecord == directories.end()) 
		{
			Log()->error("Root web directory (\"/\") is not registered");

			processError404 (context);
			return false;
		}

		DirectorySettings parentDirSettings = rootRecord->second;

		// find registered directory
		if ( !util::equals (context.InitialVirtualPath, strings::Slash) ) 
		{
			string::size_type slashPos = 0;
			directories_map::const_iterator dirIter;
			string parentDir;
			
			while ((slashPos = context.InitialVirtualPath.find(strings::Slash, slashPos + 1)) != string::npos) {
				parentDir = context.InitialVirtualPath.substr(0, slashPos + 1);
				
				if ( (dirIter = directories.find (parentDir)) != directories.end())
					parentDirSettings = dirIter->second;
				else
					break;
			} 
		}

		aconnect::ScopedMemberPointerGuard<HttpContext, const DirectorySettings*> 
			guard (&context, &HttpContext::CurrentDirectoryInfo, &parentDirSettings);

		if ( context.runModules(ModuleCallbackOnRequestResolve) )
			return false;
		

		// check request size
		if (context.RequestHeader.isContentLengthRead() 
			&& context.RequestHeader.ContentLength > parentDirSettings.maxRequestSize) 
				throw request_too_large_error (context.RequestHeader.ContentLength, 
					parentDirSettings.maxRequestSize);

		// apply mappings
		applyMappings (context, parentDirSettings);
		

		// find real path
		if (context.VirtualPath == parentDirSettings.virtualPath) {
			context.FileSystemPath = fs::path (parentDirSettings.realPath, fs::native);
		} else {
			context.FileSystemPath = fs::complete (
					fs::path (util::decodeUrl (context.VirtualPath.substr (
						parentDirSettings.virtualPath.length())), fs::portable_name), 
					fs::path (parentDirSettings.realPath, fs::native)
				);
		}
		
	
		if ( runHandlers(context, parentDirSettings) )
			return false; // processed by handler

		
		if (fs::is_directory(context.FileSystemPath) ) 
		{
			if (context.InitialVirtualPath == context.VirtualPath
				&& !algo::ends_with (context.InitialVirtualPath, strings::Slash)) {
				
				redirectRequest (context, context.InitialVirtualPath + strings::Slash); // redirect
			} else {
				
				processDirectoryRequest (context, parentDirSettings);
			}

			return false;
		}

		// find virtual dir, if found - redirect
		directories_map::const_iterator virtDirIter = directories.begin();
		const string virtDirPath = context.InitialVirtualPath + strings::Slash;
		
		while (virtDirIter != directories.end()) {
			if (virtDirIter->second.isLinkedDirectory 
				&& virtDirPath == virtDirIter->second.virtualPath) 
			{
				redirectRequest (context, virtDirPath); // redirect
				return false;
			}
			virtDirIter++;
		} 


		// only real file can be there
		if ( !fs::exists( context.FileSystemPath ) ) {
			// 404 error
			processError404 (context);
			return false;
		}

		return true;
	}

	bool HttpServer::runHandlers (HttpContext& context, const struct DirectorySettings& dirSettings)
	{
		using namespace aconnect;

		const string extension = fs::extension(context.FileSystemPath);
		directory_plugins_list::const_iterator it;
		
		Log()->debug ("Run handler for \"%s\", directory settings: \"%s\"", 
							context.FileSystemPath.string().c_str(),
							dirSettings.name.c_str());		

		for (it = dirSettings.handlers.begin(); it != dirSettings.handlers.end(); ++it)
		{
			if (it->isRequestApplicable(extension))
			{
				if ( context.runModules(ModuleCallbackOnRequestMapHandler) )
					return true;

				if (reinterpret_cast<process_request_function> (it->processFunc) (context, it->pluginIndex))
					return true;
			}
		}

		return false;
	}

		void HttpServer::processDirectoryRequest ( HttpContext& context, 
											const DirectorySettings& dirSettings)
	{
		using namespace aconnect;
		
		ProgressTimer progress (*Log(), __FUNCTION__);
		
		// find and open default document 
		for (default_documents_vector::const_iterator it = dirSettings.defaultDocuments.begin();
			it != dirSettings.defaultDocuments.end(); it++) 
		{
			fs::path docPath = context.FileSystemPath / fs::path(it->second);
			if (fs::exists (docPath)) 
			{
				context.FileSystemPath = docPath;
				context.VirtualPath += it->second;
				Log()->debug ( "Redirection to \"%s\"", context.VirtualPath.c_str() );				

				// Content-Location: <path to document>
				context.Response.Header.Headers[strings::HeaderContentLocation] = context.VirtualPath;
				
				if ( runHandlers(context, dirSettings) )
					return;
				
				processDirectFileRequest (context);
				return;
			}
		}
		
		if ( !dirSettings.browsingEnabled ) {
			// 403 error
			return processError403 (context, 
				getMessage("Error403_BrowseContent").c_str());
		}

		// HTTP method must be GET or HEAD, if not - sent 405, with "Allow: GET, HEAD"
		if (context.Method != HttpMethod::Get 
			&& context.Method != HttpMethod::Head) 
			return processError405 (context, "GET, HEAD");
		
		if ( !fs::exists( context.FileSystemPath ) ) {
			// 404 error
			return processError404 (context);
		}

		string record;
		
		if ( fs::is_directory( context.FileSystemPath ) )
		{
			// check "Accept-Charset" header
			if (context.RequestHeader.hasHeader(strings::HeaderAcceptCharset)) 
			{
				string acceptedCharsets = context.RequestHeader[strings::HeaderAcceptCharset];
				
				if ( !algo::contains (acceptedCharsets, strings::AnyContentCharsetMark) &&
					!algo::icontains (acceptedCharsets, dirSettings.charset) &&
					!algo::iequals	(strings::DefaultContentCharset, dirSettings.charset)) {

						Log()->error ("Charset \"%s\" is not allowed in \"%s\"", 
							dirSettings.charset.c_str(),
							acceptedCharsets.c_str());
						
						return processError406 (context, 
							getMessage("Error406_CharsetNotAllowed"));
				}

			}

			// start response
			context.Response.Header.Status = 200;
			context.Response.Header.setContentType (strings::ContentTypeTextHtml, dirSettings.charset);

			// format header
			context.Response.write (formatHeaderRecord (dirSettings, context.InitialVirtualPath));

			if ( !util::equals (context.InitialVirtualPath, strings::Slash)) {
				string parentDir = context.InitialVirtualPath.substr (0, context.InitialVirtualPath.rfind (strings::SlashCh, context.InitialVirtualPath.size() - 2) + 1);
				context.Response.write (formatParentDirRecord (dirSettings, parentDir ));
			}
			
			std::vector<WebDirectoryItem> directoryItems;

			// write virtual directories
			const directories_map &directories = GlobalSettings()->Directories();
			directories_map::const_iterator virtDirIter = directories.begin();

			while (virtDirIter != directories.end()) {
				if (virtDirIter->second.isLinkedDirectory &&
						virtDirIter->second.parentName == dirSettings.name &&
						context.InitialVirtualPath == dirSettings.virtualPath) 
				{
					WebDirectoryItem item;
					item.url = virtDirIter->second.virtualPath;
					item.name = virtDirIter->second.relativePath;
					item.type = WdVirtualDirectory;
					item.lastWriteTime = fs::last_write_time (virtDirIter->second.realPath);

					directoryItems.push_back (item);
				}

				virtDirIter++;
			} 

			size_t fileCount = 0, dirCount = 0, errCount = 0;

			// get filesystem items
			readDirectoryContent (context.FileSystemPath.string(), 
					context.InitialVirtualPath,
					directoryItems,
					*Log(),
					errCount,
					context.Client->server,
					WdSortByTypeAndName);
			
			// write content
			
			for ( std::vector<WebDirectoryItem>::const_iterator itemIter = directoryItems.begin();
				itemIter != directoryItems.end();
				++itemIter )
			{
				
				if (itemIter->type == WdVirtualDirectory) {
					record = formatItemRecord (dirSettings.virtualDirectoryTemplate,
						itemIter->url,
						itemIter->name, 
						(size_t) itemIter->size,
						itemIter->lastWriteTime);
				
				} else if (itemIter->type == WdDirectory) {
					record = formatItemRecord (dirSettings.directoryTemplate,
						itemIter->url,
						itemIter->name, 
						(size_t)itemIter->size,
						itemIter->lastWriteTime);
					
					++dirCount;

				} else {
					record = formatItemRecord (dirSettings.fileTemplate,
							itemIter->url,
							itemIter->name,
							(size_t)itemIter->size,
							itemIter->lastWriteTime);
					++fileCount;
				}

				context.Response.write (record);
			}

			// format footer
			context.Response.write (formatFooterRecord (dirSettings, context.InitialVirtualPath,
				fileCount, dirCount, errCount));
			context.Response.end();

		} else {
			Log()->error ("%s: file path retrieved instead of directory - \"%s\"", 
				__FUNCTION__, 
				context.FileSystemPath.string().c_str());
			
			processServerError(context, ahttp::HttpStatus::InternalServerError, getMessage("ServerError_FileInsteadDirectory").c_str());
		}
	}

	void HttpServer::processDirectFileRequest (HttpContext& context) 
	{
		using namespace aconnect;
		ProgressTimer progress (*Log(), __FUNCTION__);

		if ( context.runModules(ModuleCallbackOnRequestMapHandler) )
			return;
				
		// HTTP method must be GET or HEAD, if not - sent 405, with "Allow: GET, HEAD"
		if (context.Method != HttpMethod::Get 
			&& context.Method != HttpMethod::Head) 
			return processError405 (context, "GET, HEAD");

		const string filePath = context.FileSystemPath.file_string();

		std::ifstream file (filePath.c_str());
		if ( file.fail() ) 
		{
			// Access denied (404 checked previously)
			processError403(context, getMessage("Error403_AccessDenied").c_str());
			return;
		}
		file.close();
		
		std::time_t modifyTime = fs::last_write_time ( context.FileSystemPath);
		std::streamsize fileSize = (std::streamsize) fs::file_size (context.FileSystemPath);
		bool loadRange = true; // process 'Range: XXX-YYY' if exists

		string etag = util::calculateFileCrc (modifyTime, fileSize);
		
		// add ETag
		context.Response.Header.Headers[strings::HeaderETag] = etag;
		
		// process "If-Modified-Since" header
		if (context.RequestHeader.hasHeader (strings::HeaderIfModifiedSince) ) 
		{
			std::time_t inputModifyTime = getDateFrom_RFC1123 (context.RequestHeader.getHeader(strings::HeaderIfModifiedSince));
			if (modifyTime <= inputModifyTime)
			{
				context.Response.Header.Status = 304;
				context.Response.Header.setContentLength ( 0 );
				return;
			}
		}
		// process "If-None-Match"
		else if (context.RequestHeader.hasHeader (strings::HeaderIfNoneMatch) ) 
		{
			if (etag == context.RequestHeader.Headers[strings::HeaderIfNoneMatch])
			{
				context.Response.Header.Status = 304;
				context.Response.Header.setContentLength ( 0 );
				return;
			}
		}
		else if (context.RequestHeader.hasHeader (strings::HeaderIfRange) ) 
		{
			if (!util::equals (context.RequestHeader.getHeader (strings::HeaderIfRange), etag) )
				loadRange = false;
		}
		else if (context.RequestHeader.hasHeader (strings::HeaderIfUnmodifiedSince) ) 
		{
			std::time_t inputModifyTime = getDateFrom_RFC1123 (context.RequestHeader.getHeader(strings::HeaderIfUnmodifiedSince));
			if (modifyTime > inputModifyTime)
				loadRange = false;
		}
		else if (context.RequestHeader.hasHeader (strings::HeaderIfMatch) ) 
		{
			// RFC 2616: 14.24 If-Match - send 412 (Precondition Failed) if entity is not the same
			if (!util::equals (context.RequestHeader.getHeader (strings::HeaderIfMatch), etag) ) 
			{
				context.Response.Header.Status = HttpStatus::PreconditionFailed;
				return;
			}
		}

		Log()->debug ("Send file: %s", filePath.c_str());
		
		sendFileToClient (context, fileSize, filePath, modifyTime, loadRange);
	}


	void HttpServer::sendFileToClient (HttpContext& context, 
			std::streamsize fileSize, 
			string_constref filePath, 
			std::time_t modifyTime,
			bool loadRange) 
	{
		using namespace aconnect;

		// prepare response
		context.Response.Header.Status = HttpStatus::OK;
		context.Response.Header.setContentLength ( fileSize );
		context.Response.Header.setContentType ( context.GlobalSettings->getMimeType (
			fs::extension (context.FileSystemPath) ).c_str() );
		context.Response.Header.Headers[strings::HeaderLastModified] = formatDate_RFC1123 (util::getDateTimeUtc (modifyTime));
		context.Response.Header.Headers[strings::HeaderAcceptRanges] = strings::AcceptRangesBytes;
		
		// send file
		if (fileSize > 0) 
		{
			std::streamsize fileOffset = 0,
				requestedAmount = fileSize;
			
			if (loadRange && context.RequestHeader.hasHeader (strings::HeaderRange)) 
			{
				bool rangeValid = true;
				string range = context.RequestHeader.getHeader (strings::HeaderRange);
				
				range = range.substr (strlen(strings::AcceptRangesBytes) + 1); // "bytes="
				
				if (range.empty())
					rangeValid = false;

				if (rangeValid) 
				{
					string::size_type pos = range.find ("-");
					
					if (pos == 0) { // "-XXX" - The final XXX bytes 
						requestedAmount = boost::lexical_cast<std::streamsize> (range.substr (1));
						fileOffset = fileSize - requestedAmount;
						
						if (fileOffset <= 0)
							rangeValid = false;

					} else if (pos == range.size() - 1) { // "XXX-" All bytes from XXX

						fileOffset = boost::lexical_cast<std::streamsize> (range.substr (0, range.size() - 1));
						requestedAmount = fileSize - fileOffset;
						
						if (requestedAmount <= 0)
							rangeValid = false;

					} else if (pos == string::npos) {
						rangeValid = false;

					} else { // "XXX-YYY" - bytes from XXX to YYY

						fileOffset = boost::lexical_cast<std::streamsize> (range.substr (0, pos));
						requestedAmount = boost::lexical_cast<std::streamsize> (range.substr (pos + 1)) - fileOffset + 1;
						
						if (fileOffset + requestedAmount >= fileSize)
							rangeValid = false;
					}
				}
				
				if (!rangeValid) {
					context.Response.Header.Status = HttpStatus::RequestedRangeNotSatisfiable;
					context.Response.Header.Headers[strings::HeaderContentRange] = strings::AcceptRangesBytes + 
						string(" */") + boost::lexical_cast<string> (fileSize);

					return;
				}


				aconnect::str_stream contentRange;

				contentRange << strings::AcceptRangesBytes << " " << fileOffset << "-"
					<< (fileOffset + requestedAmount - 1) << "/" << fileSize;

				context.Response.Header.Status = HttpStatus::PartialContent;
				context.Response.Header.Headers[strings::HeaderContentRange] = contentRange.str();
			}

			if (context.Method == HttpMethod::Head) 
				// IMPORTANT: all headers collected, in case empty file correct
				// response will be sent
				return;
	
			const std::streamsize buffSize = (std::streamsize) util::min2( requestedAmount, 
				(std::streamsize) context.Response.Stream.getBufferSize());
			boost::scoped_array<char_type> buff (new char_type [buffSize]);
			
			std::ifstream file (filePath.c_str(), std::ios::binary);

			if (fileOffset != 0)
				file.seekg (fileOffset, std::ios_base::beg );

			std::streamsize readBytes = 0, 
				totalRead = 0;
			do 
			{
				assert (file.good());

				file.read (buff.get(), buffSize);

				readBytes = file.gcount();
				totalRead += readBytes;

				context.Response.write (buff.get(), readBytes);

			} while (totalRead < requestedAmount);

			file.close();
		}
	}


	string HttpServer::formatHeaderRecord (const DirectorySettings& dirSettings, 
													   string_constref virtualPath) 
	{
		string record = algo::replace_all_copy (dirSettings.headerTemplate,
		   SettingsTags::PageUrlMark, virtualPath);
		return record;
	}

	string HttpServer::formatParentDirRecord (const DirectorySettings& dirSettings, 
													   string_constref parentPath) 
	{
		string record = algo::replace_all_copy (dirSettings.parentDirectoryTemplate,
			SettingsTags::ParentUrlMark, parentPath);
		return record;
	}

	string HttpServer::formatItemRecord (string_constref itemTemplate,
		string_constref itemUrl,
		string_constref itemName,
		const size_t itemSize,
		std::time_t lastWriteTime) 
	{
		string record = algo::replace_all_copy (itemTemplate,
			SettingsTags::UrlMark, itemUrl);
		algo::replace_all (record, SettingsTags::NameMark, itemName);

		if (itemSize != (size_t)-1) 
		{
			string size = boost::lexical_cast<string> (itemSize);
			const size_t minWidth = 16;

			if (size.size() < minWidth)
				size.insert(0, minWidth - size.size(), ' ');

			algo::replace_all (record, SettingsTags::SizeMark, size);
		}

		if (lastWriteTime != (std::time_t) -1)
			algo::replace_all (record, SettingsTags::TimeMark,
			formatDateTime ( aconnect::util::getDateTimeUtc(lastWriteTime) ) );

		return record;
	}

	string HttpServer::formatFooterRecord (const DirectorySettings& dirSettings, 
													   string_constref virtualPath,
													   const size_t fileCount, const size_t dirCount, const size_t errCount) 
	{
		string record = algo::replace_all_copy (dirSettings.footerTemplate,
			SettingsTags::PageUrlMark, virtualPath);

		algo::replace_all (record, SettingsTags::FilesCountMark, 
			boost::lexical_cast<string> (fileCount) );
		algo::replace_all (record, SettingsTags::DirectoriesCountMark, 
			boost::lexical_cast<string> (dirCount) );
		algo::replace_all (record, SettingsTags::ErrorsCountMark, 
			boost::lexical_cast<string> (errCount) );

		return record;
	}
}


