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
#include <boost/scoped_array.hpp>
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
	
#include "http_header_read_check.inl"

	//////////////////////////////////////////////////////////////////////////
	//		UploadFileInfo class
	//////////////////////////////////////////////////////////////////////////
	void UploadFileInfo::loadHeader (string_constref header)
	{
		using namespace aconnect;

		reset();

		str_vector lines;
		string name, value;
		algo::split (lines, header, algo::is_any_of("\r\n"), algo::token_compress_on);

		size_t pos;
		std::map<string, string> pairs;

		for (str_vector::iterator it = lines.begin(); it != lines.end(); ++it) 
		{
			if (it->empty())
				continue;

			if ((pos = it->find (':')) == string::npos) 
				throw request_processing_error ("Incorrect part header: %s", it->c_str());

			name = it->substr (0, pos);
			value = algo::trim_copy(it->substr (pos + 1));

			if (util::equals (name, strings::HeaderContentDisposition)) 
			{
				if ( !algo::starts_with(value, strings::ContentDispositionFormData) )
					throw request_processing_error ("Incorrect Content-Disposition type: %s", it->c_str());

				util::parseKeyValuePairs (value, pairs);

				if (pairs.find("name") == pairs.end() )
					throw request_processing_error ("'name' attribute is absent in Content-Disposition header: %s", it->c_str());

				this->name = pairs["name"];

				std::map<string, string>::iterator fnIter = pairs.find ("filename");
				if ( fnIter != pairs.end()) {
					this->isFileData = true;
					this->fileName = fnIter->second;
					this->shortFileName = fs::path(this->fileName).leaf();
				}


			} else if (util::equals (name, strings::HeaderContentType)) {
				this->contentType = value;
			}
		}
	}

	void UploadFileInfo::reset ()
	{
		isFileData = false;
		fileSize = 0;
		name.clear();
		fileName.clear();
		contentType.clear();
		uploadPath.clear();
	}

//////////////////////////////////////////////////////////////////////////
//		HttpContext class
//////////////////////////////////////////////////////////////////////////

	HttpContext::HttpContext (const aconnect::ClientInfo* clientInfo, 
			HttpServerSettings* globalSettings,
			aconnect::Logger *log) :
		Client (clientInfo),
		RequestHeader (),
		RequestStream (),
		Response (globalSettings ? globalSettings->responseBufferSize() : ahttp::defaults::ResponseBufferSize, 
			globalSettings ? globalSettings->maxChunkSize() : ahttp::defaults::MaxChunkSize),
		Method (HttpMethod::Unknown),
		GlobalSettings (globalSettings),
		Log (log),
		CurrentDirectoryInfo (NULL),
		IsKeepAliveConnect (false)
	{
		assert (clientInfo);
		assert (globalSettings);
		assert (log);
	}

	HttpContext::~HttpContext()
	{
		std::map <string, UploadFileInfo>::const_iterator iter;
		for (iter = UploadedFiles.begin(); iter != UploadedFiles.end(); ++iter)
		{
			try	{
				fs::remove(iter->second.uploadPath);
			} catch (...)  {
				processException ("Upload deletion failed", false);
			}
		}
		
	}

	bool HttpContext::init (bool isKeepAliveConnect,
							long keepAliveTimeoutSec) {
		
		HttpHeaderReadCheck check (&RequestHeader, Client->server, 
			isKeepAliveConnect, keepAliveTimeoutSec);

		string requestBodyBegin = aconnect::util::readFromSocket (Client->sock, check, false);
		if (check.connectionWasClosed() || requestBodyBegin.empty())
			return false;

		boost::algorithm::erase_head ( requestBodyBegin, (int) check.headerSize());
		RequestStream.init (requestBodyBegin, (int) RequestHeader.ContentLength, Client->sock);

		Response.init (this, Client);
		Response.setServerName (GlobalSettings->serverVersion());
		
		IsKeepAliveConnect = isKeepAliveConnect;
		return true;
	}

	void HttpContext::reset () 
	{
		RequestHeader.clear();
		RequestStream.clear();
		Response.clear();
		
		GetParameters.clear();
		PostParameters.clear();
		Cookies.clear();
		
		UploadedFiles.clear();
		
		Method = HttpMethod::Unknown;

		CurrentDirectoryInfo  = NULL;
		IsKeepAliveConnect = false;
	}
	
	void HttpContext::setHtmlResponse() {
		if (Response.isFinished())
			return;

		if ( Response.Header.Status == HttpResponseHeader::UnknownStatus )
			Response.Header.Status = 200;
		if ( !Response.Header.hasHeader (strings::HeaderContentType) )
			Response.Header.setContentType (strings::ContentTypeTextHtml);
	}
	
	bool HttpContext::isClientConnected()  const
	{
		if (this->Client->server->isStopped())
			return false;

		if (RequestStream.hasBufferedContent())
			return true;

		if (!RequestStream.isRead())
			return aconnect::util::checkSocketState (RequestStream.socket(),
				GlobalSettings->serverSettings().socketReadTimeout);
		
		return aconnect::util::checkSocketState (Response.Stream.socket(),
			GlobalSettings->serverSettings().socketWriteTimeout,
			true);
	}

	void HttpContext::closeConnection (bool closeSocket)
	{
		Response.setFinished();
		Client->close(closeSocket);
	}

	void HttpContext::parseQueryStringParams ()
	{
		using namespace aconnect;
		if (QueryString.empty())
			return;

		str_vector pairs;
		size_t pos;
		algo::split (pairs, QueryString, boost::algorithm::is_any_of("&"), algo::token_compress_on);

		for (str_vector::iterator it = pairs.begin() ; it != pairs.end(); ++it) {
			pos = it->find ('=');
			if (pos == string::npos) {
				GetParameters[util::decodeUrl(*it)] = "";
			} else {
				GetParameters[util::decodeUrl(it->substr (0, pos))] = util::decodeUrl(it->substr (pos + 1));
			}
		}
	}

	void HttpContext::parseCookies () {
		// HTTP header: "Cookie: PART_NUMBER=RIDING_ROCKET_0023; PART_NUMBER=ROCKET_LAUNCHER_0001"
		using namespace aconnect;
		if ( !RequestHeader.hasHeader (strings::HeaderCookie) )
			return;

		string cookiesString = RequestHeader.getHeader(strings::HeaderCookie);
		
		str_vector pairs;
		algo::split (pairs, cookiesString, boost::algorithm::is_any_of(";"), algo::token_compress_on);

		for (str_vector::iterator it = pairs.begin() ; it != pairs.end(); ++it) {
			string::size_type pos = it->find ('=');
			if (pos == string::npos) {
				Cookies [util::decodeUrl(*it)] = "";
			} else {
				Cookies [util::decodeUrl(it->substr (0, pos))] = util::decodeUrl(it->substr (pos + 1));
			}
		}
	}

	void HttpContext::loadPostParams() 
	{
		using namespace aconnect;

		string contentType = RequestHeader.getHeader(strings::HeaderContentType);
		
		if (algo::istarts_with (contentType, strings::ContentTypeMultipartFormData) ) {
			
			string boundary = contentType.substr (contentType.find (strings::MultipartBoundaryMark) +
					ARRAY_SIZE(strings::MultipartBoundaryMark) - 1);
			
			return loadMultipartFormData (boundary);
		}
		
		if (RequestHeader.ContentLength == 0)
			return;

		const int buffSize = (int) util::min2 (Response.Stream.getBufferSize(), RequestHeader.ContentLength);
		boost::scoped_array<char_type> buff (new char_type [buffSize]);
		
		//util::zeroMemory ( (void*) buff.get(), buffSize);
		
		int readBytes = 0, pos = 0;
		
		string key, val;
		char_type ch;
		bool keyLoaded = false;
		size_t loadedContentLength = 0;
		
		val.reserve (buffSize);
		
		do 
		{
			readBytes = RequestStream.read((string_ptr) buff.get(), buffSize);
			loadedContentLength += readBytes;

			if (readBytes > 0) 
			{
				pos = 0;

				while (pos < readBytes) {
					ch = buff[pos++];
					
					if ( ch == '&' && !key.empty() ) {
						PostParameters [util::decodeUrl(key)] = util::decodeUrl (val);
						key.clear();
						val.clear();
						keyLoaded = false;
					
					} else if (ch == '=') {
						keyLoaded = true;
					
					} else if (keyLoaded) {
						val.append(1, ch);
					} else {
						key.append(1, ch);
					}
				
				};
			
			} else if (!key.empty() && loadedContentLength == RequestHeader.ContentLength) {
				PostParameters [util::decodeUrl(key)] = util::decodeUrl (val);
			}
		
		} while (readBytes > 0);
		
	}

	
	void HttpContext::loadMultipartFormData (string_constref boundary) 
	{
		using namespace aconnect;

		const int buffSize = (int) util::min2 (Response.Stream.getBufferSize(), RequestHeader.ContentLength);

		boost::scoped_array<char_type> buff (new char_type [buffSize]);

		int readBytes = 0;
		string record, fieldName;
		size_t boundaryPos = 0, 
			endPos = 0, 
			readContentLength = 0;
		fs::path uploadPath;
		
		const string boundaryBegin = strings::MultipartBoundaryPrefix + boundary;
		const string boundaryBeginWithEndMark = string(strings::HeadersDelimiter) + strings::MultipartBoundaryPrefix + boundary;
		const string boundaryEnd = strings::MultipartBoundaryPrefix + boundary + strings::MultipartBoundaryPrefix;

		const size_t boundOffset = (boundaryBegin + strings::HeadersDelimiter).size();
		const size_t endMarkLen = strlen (strings::HeadersEndMark);
		const size_t headerEndMarkLen = strlen (strings::HeadersDelimiter);

		UploadFileInfo uploadInfo;
		std::ofstream currentFile;
		string storedFileName,
				errorMessage;

		time_t curTime = time(NULL);
		srand ( (unsigned) curTime);

		string timestamp = boost::str(boost::format("%016.X") % curTime);

		size_t maxRequestSize = defaults::MaxRequestSize;
		if (CurrentDirectoryInfo)
			maxRequestSize = CurrentDirectoryInfo->maxRequestSize;

		do 
		{
			readBytes = RequestStream.read (buff.get(), buffSize);
			
			readContentLength += readBytes;

			// check request size
			if (readContentLength > maxRequestSize) 
				throw request_too_large_error (readContentLength, 
					maxRequestSize);
		
			record.append (buff.get(), readBytes);
			boundaryPos = record.find (boundaryBegin);

			
			while (record.length()) 
			{
				// start reading
				if (boundaryPos == 0 
					&& (endPos = record.find (strings::HeadersEndMark, 0)) != string::npos) 
				{
					if (currentFile.is_open()) {
						currentFile.flush();
						currentFile.rdbuf()->close();
					}

					uploadInfo.loadHeader (record.substr(boundOffset, endPos - boundOffset));
					fieldName = util::decodeUrl (uploadInfo.name);

					record.erase(0, endPos + endMarkLen);
					boundaryPos = util::findSequence (record, boundaryBeginWithEndMark);
					
					if ( !uploadInfo.isFileData ) {
						if (!isClosed() && !Response.isFinished())
							PostParameters [fieldName] += record.substr (0, boundaryPos);
			
					} else {
						
						// open file stream to store uploaded item
						if (!isClosed() && !Response.isFinished() && !uploadInfo.fileName.empty() ) 
						{
							storedFileName = timestamp;

							try
							{
								if (UploadsDirPath.empty()) 
								{
									if (!GlobalSettings->globalUploadsDirectory().empty()) {
										UploadsDirPath = fs::path(GlobalSettings->globalUploadsDirectory(), fs::native);

									} else {
										Log->error ("Uploads folder is not set up, uploaded files will be skipped, target: %s", 
											VirtualPath.c_str());
									}
								}
							
								if (!UploadsDirPath.empty()) 
								{
									// fast upload name search
									
									do {
										uploadPath = UploadsDirPath / storedFileName;
										storedFileName = timestamp + boost::str(boost::format("_%08.X") % rand());
									
									} while (fs::exists (uploadPath));

									// open file
									int tryCount = 0; 
									
									const int maxTryCount = GlobalSettings->uploadCreationTriesCount(); 

									do {
										uploadPath = UploadsDirPath / storedFileName;
										currentFile.open ( uploadPath.file_string().c_str(), std::ios::out | std::ios::binary );
										storedFileName = timestamp + boost::str(boost::format("_%08.X") % rand());

									} while ((currentFile.fail() || currentFile.bad()) && ++tryCount < maxTryCount);
								
									if (tryCount >= maxTryCount) {
										Log->error ("Cannot create upload file: %s", uploadPath.file_string().c_str());
										errorMessage = "Cannot store upload file: " +  uploadInfo.shortFileName + "\r\n";
										
										uploadInfo.isFileData = false;
										uploadInfo.uploadPath.clear ();
									}

									if (currentFile.good())
										uploadInfo.uploadPath = uploadPath.normalize().file_string();
								}

							} catch (...) {
								uploadInfo.isFileData = false;
								uploadInfo.uploadPath.clear ();

								return processException("Uploaded file storing");
							}
							
							if (currentFile.good() && record.size()) 
								currentFile << record.substr (0, boundaryPos);
							
						}

						UploadedFiles[fieldName] = uploadInfo;
					}

					record.erase(0, boundaryPos);
					boundaryPos = 0;
				
				} else if (!fieldName.empty() && boundaryPos != 0 && boundaryPos != string::npos) {
					
					if (!isClosed() && !Response.isFinished())
					{
						if ( !uploadInfo.isFileData ) {
							PostParameters [fieldName] += record.substr (0, boundaryPos - headerEndMarkLen);
						} else if (currentFile.good()) {
							currentFile << record.substr (0, boundaryPos - headerEndMarkLen);
						}
					}
					
					record.erase(0, boundaryPos);
					boundaryPos = 0;

				} else { 
					
					
					if (!isClosed() 
						&& !Response.isFinished()
						&& uploadInfo.isFileData
						&& currentFile.good()
						&& currentFile.is_open()
						&& util::findSequence (record, boundaryBeginWithEndMark) == string::npos
						&& util::findSequence (record, boundaryBegin) == string::npos) 
					{
						currentFile << record;
						record.clear();
									
					} else {
						break;
					}
				}
				
				if (record.find (boundaryEnd) == 0) {
					// eat request
					while (!RequestStream.isRead() && readBytes > 0)
						readBytes = RequestStream.read (buff.get(), buffSize);
					
					readBytes = 0;
					break;
				}
			}
			
		} while (readBytes > 0);

		if (currentFile.is_open()) {
			currentFile.flush();
			currentFile.rdbuf()->close();
		}

		if (!errorMessage.empty())
			return HttpServer::processServerError(*this, HttpStatus::InternalServerError, errorMessage.c_str() );

		if (isClosed())
			return;
				
		// read files info
		std::map <string, UploadFileInfo>::iterator iter;
		for (iter = UploadedFiles.begin(); iter != UploadedFiles.end(); ++iter)
		{
			if (iter->second.uploadPath.empty())
				continue;

			try	{
				iter->second.fileSize = (size_t) 
					fs::file_size (iter->second.uploadPath);
			
			} catch (...)  {
				iter->second.isFileData = false;
				iter->second.uploadPath.clear ();

				return processException ("Uploaded file properties loading failed");
			}
		}
	}

	void HttpContext::processException (string_constptr message, bool sendResponse,  HttpStatus::HttpStatusType status)
	{
		if (Log)
		{
			try
			{
				throw;

			} catch (fs::basic_filesystem_error<fs::path> &err) {
				Log->error (
					"%s (%s) - 'basic_filesystem_error' caught: %s, "
					"system error code: %d, path 1: %s, path 2: %s", 
					message, VirtualPath.c_str(),
					err.what(), err.system_error(),
					err.path1().string().c_str(),
					err.path2().string().c_str()); 

			} catch (fs::filesystem_error &err) {
				Log->error (
					message, VirtualPath.c_str(),
					"%s (%s) - 'filesystem_error' caught: %s, "
					"system error code: %d", 
					err.what(), err.system_error());

			} catch (std::exception const &ex) {
				Log->error ("%s (%s): exception caught (%s): %s", 
					message, VirtualPath.c_str(), typeid(ex).name(), ex.what());
			
			} catch (...)  {
				Log->error ("%s (%s): unknown exception caught", message, VirtualPath.c_str());
			}
		}

		// try to send response
		try
		{
			if (sendResponse) {
				string errMessage = boost::str(boost::format (HttpServer::getMessage("Error500_ProcessingFailed")) % InitialVirtualPath);
				HttpServer::processServerError (*this, status, errMessage.c_str() );
			}
		} catch (...) {
			// eat exception - it is in log already	
		}

		Response.end();
	}

	string HttpContext::mapPath (string_constptr virtualPath, bool& fileExists) 
		const throw (std::runtime_error)
	{
		using namespace aconnect;

		fs::path realPath;

		if (util::isNullOrEmpty (virtualPath) || util::equals(virtualPath, ".") ) {
			realPath =  FileSystemPath;

		} else {
			if (NULL == CurrentDirectoryInfo) 
				throw std::runtime_error ("Directory info for context is not loaded");

			if (util::equals(virtualPath, "/") ) {
				realPath = CurrentDirectoryInfo->realPath;

			} else if (virtualPath[0] == '/') { // absolute from root path
				string url = util::decodeUrl (virtualPath);
				if (algo::starts_with(url, CurrentDirectoryInfo->virtualPath))
					url.erase(0, CurrentDirectoryInfo->virtualPath.size());
				else
					url.erase(0, 1);
				
				realPath = fs::complete ( 
						fs::path (url, fs::portable_name), 
						fs::path (CurrentDirectoryInfo->realPath, fs::native)
					).normalize();
			
			} else {
				realPath = fs::complete ( 
						fs::path (util::decodeUrl (virtualPath), fs::portable_name), 
						FileSystemPath
					).normalize();
			}
			
			if (realPath.string().size() < CurrentDirectoryInfo->realPath.size()
				&& !CurrentDirectoryInfo->isParentPathAccessEnabled() )
					throw std::runtime_error ("Access to non-application items denied");
		}

		fileExists = fs::exists (realPath);
		string result = realPath.file_string();

		if (!fs::is_directory(realPath))
			return result;

		if (algo::ends_with (result, ".")) {
			result.erase (result.size() - 1);
			return result;
		}
		
#ifdef BOOST_WINDOWS_PATH
		if (!algo::ends_with (result, strings::WindowsSlash))
			result += strings::WindowsSlash;	
#else
		if (!algo::ends_with (result, strings::Slash))
			result += strings::Slash;
#endif
		return result;
	}


	string HttpContext::getServerVariable (string_constptr variableName)
	{
		using namespace aconnect;
		using namespace strings::ServerVariables;
		str2str_map::const_iterator iter;
		
		if (util::isNullOrEmpty(variableName))
			throw request_processing_error ("Empty variable name to load, path: %s",
				VirtualPath.c_str());

		// non-cacheable vars
		
		if (util::equals (variableName, APPL_PHYSICAL_PATH)) {
			return mapPath("/");
		
		} else if (util::equals (variableName, CONTENT_LENGTH)) {
			if (RequestHeader.isContentLengthRead())
				return boost::lexical_cast<string> (RequestHeader.ContentLength);
		
		} else if (util::equals (variableName, CONTENT_TYPE)) {
			if (Response.Header.hasHeader (strings::HeaderContentType))
				return Response.Header.Headers[strings::HeaderContentType];
	
		} else if (util::equals (variableName, PATH_INFO) 
			|| util::equals (variableName, SCRIPT_NAME)
			|| util::equals (variableName, URL)) {
			
				if ((iter = Items.find(URL_ENCODED)) != Items.end()) {
					if (util::equals (iter->second, VirtualPath))
						return Items[URL];
				}
				
				Items[URL_ENCODED] = VirtualPath;
				Items[URL] =  util::decodeUrl(VirtualPath);

				return Items[URL];

		} else if (util::equals (variableName, PATH_INFO_RELATIVE)) {
			return  (VirtualPath.size() && VirtualPath[0] == '/' ? VirtualPath.substr(1) : VirtualPath);
					
		} else if (util::equals (variableName, PATH_TRANSLATED)) {
			string filePath = FileSystemPath.file_string();
#ifdef WIN32
			std::replace (filePath.begin(), filePath.end(), '/', '\\');
#endif
			return  filePath;
		
		} else if (util::equals (variableName, QUERY_STRING)) {
			return  QueryString;
		
		} else if (util::equals (variableName, REQUEST_METHOD)) {
			return  RequestHeader.Method;

		}

		iter = Items.find(variableName);
		if (iter != Items.end() )
			return iter->second;

		// setup defauts
		string var;

		if (util::equals (variableName, ALL_RAW)) {
			
			var.reserve (RequestHeader.Headers.size() * 64);
			aconnect::str2str_map_ci::const_iterator it = RequestHeader.Headers.begin();
			
			for (; it != RequestHeader.Headers.end(); ++it) {
				var.append (it->first);
				var.append (": ");
				var.append (it->second);
				var.append ("\r\n");
			}

		/*
			Should be stored by modules:
				AUTH_PASSWORD, AUTH_TYPE, AUTH_USER, 
				CERT_COOKIE, CERT_FLAGS, CERT_ISSUER, CERT_KEYSIZE, CERT_SECRETKEYSIZE, CERT_SERIALNUMBER, 
				CERT_SERVER_ISSUER, CERT_SERVER_SUBJECT, CERT_SUBJECT, 
		*/
				
		
		} else if (util::equals (variableName, GATEWAY_INTERFACE)) {
			// Should be updated by handler module
					
		} else if (util::equals (variableName, HTTPS)) {
			var = strings::HttpsDisabledValue; 
			// Can be updated by module
			// with HTTPS_KEYSIZE, HTTPS_SECRETKEYSIZE, HTTPS_SERVER_ISSUER, HTTPS_SERVER_SUBJECT:

		} else if (util::equals (variableName, INSTANCE_ID)) {
			var = boost::lexical_cast<string> (GlobalSettings->InstanceId);
		
		} else if (util::equals (variableName, LOCAL_ADDR)) {
			if ( util::isLocalhostIpAddress(Client->server->settings().ip) )
				var = util::formatIpAddr(aconnect::network::LocalhostAddress);
			else
				var = util::formatIpAddr(Client->server->settings().ip);
		
		} else if (util::equals (variableName, REMOTE_ADDR) ||
			util::equals (variableName, REMOTE_HOST)) {
				var = util::formatIpAddr(Client->ip);
		
		} else if (util::equals (variableName, SERVER_NAME)) {
			if (Client->server->settings().resolvedHostName.empty() && 
				util::isLocalhostIpAddress(Client->server->settings().ip))
					var = aconnect::network::LocalhostName;
			else
				var = Client->server->settings().resolvedHostName;

		} else if (util::equals (variableName, SERVER_PORT)) {
			var = boost::lexical_cast<string> (Client->server->port());

		} else if (util::equals (variableName, SERVER_PORT_SECURE)) {
			var = "0";

		} else if (util::equals (variableName, SERVER_PROTOCOL)) {
			var = strings::HttpVersion;

		} else if (util::equals (variableName, SERVER_SOFTWARE)) {
			var = GlobalSettings->serverVersion();

		} else if (util::equals (variableName, HTTP_ACCEPT)) {
			var = RequestHeader.getHeader (strings::HeaderAccept);
		
		} else if (util::equals (variableName, HTTP_ACCEPT_LANGUAGE)) {
			var = RequestHeader.getHeader (strings::HeaderAcceptLanguage);

		} else if (util::equals (variableName, HTTP_COOKIE)) {
			var = RequestHeader.getHeader (strings::HeaderCookie);
			
		} else if (util::equals (variableName, HTTP_CONNECTION)) {
			if (RequestHeader.hasHeader(strings::HeaderProxyConnection))
				var = RequestHeader.getHeader (strings::HeaderProxyConnection);
			else
				var = RequestHeader.getHeader (strings::HeaderConnection);

		} else if (util::equals (variableName, HTTP_HOST)) {
			var = RequestHeader.getHeader (strings::HeaderHost);
		
		} else if (util::equals (variableName, HTTP_REFERER)) {
			var = RequestHeader.getHeader (strings::HeaderReferer);
		
		} else if (util::equals (variableName, HTTP_USER_AGENT)) {
			var = RequestHeader.getHeader (strings::HeaderUserAgent);
		
		} else if (util::equals (variableName, HTTP_ACCEPT_ENCODING)) {
			var = RequestHeader.getHeader (strings::HeaderAcceptEncoding);
		} else if (util::equals (variableName, HTTP_ACCEPT_CHARSET)) {
			var = RequestHeader.getHeader (strings::HeaderAcceptCharset);
		} else if (util::equals (variableName, HTTP_KEEP_ALIVE)) {
			var = RequestHeader.getHeader (strings::HeaderKeepAlive);
	
		}
	
		// cache it
		Items[variableName] = var;

		return var;
	}


	bool HttpContext::runModules (ModuleCallbackType callbackType)
	{
		using namespace aconnect;

		assert (Log && GlobalSettings && "HttpContext was not initializaed correctly");
		
		Log->debug ("Run modules for \"%s\", type: %d", 
							RequestHeader.Path.c_str(),
							callbackType);		
		
		
		const directories_callback_map& modulesCallbacks = GlobalSettings->modulesCallbacks();
		directories_callback_map::const_iterator callbacksIt = 
			modulesCallbacks.find (CurrentDirectoryInfo ? CurrentDirectoryInfo->number : 0); 
		// root dir has number == 0 

		assert (callbacksIt != modulesCallbacks.end());
		
		const callback_map& callbackMap = callbacksIt->second;
		callback_map::const_iterator it = callbackMap.find (callbackType);

		// there are registered callbacks
		if (it != callbackMap.end())
		{
			std::list< std::pair <void*, int> >::const_iterator cbIter = it->second.begin();
			for (; cbIter != it->second.end(); ++cbIter)
			{
				if (reinterpret_cast<module_callback_function> (cbIter->first) (*this, cbIter->second))
					return true;
			}
		
		}
	
		return false;
	}


}
