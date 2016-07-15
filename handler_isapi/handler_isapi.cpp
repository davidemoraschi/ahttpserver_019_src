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

// handler_isapi.cpp : Defines the entry point for the DLL application.
//

#ifdef WIN32
#include <boost/thread.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>


#include "ahttplib.hpp"
#include "aconnect/util.hpp"
#include "aconnect/util.string.hpp"
#include "aconnect/util.file.hpp"

#include <httpext.h>

#ifndef HSE_REQ_EXTENSION_TRIGGER
#define HSE_REQ_EXTENSION_TRIGGER	(HSE_REQ_END_RESERVED + 20)
#endif


namespace fs = boost::filesystem;

struct DllRecord
{
	fs::path Path;
	HMODULE Handle;
	bool FreeLibraryAtReload;
	bool CheckThatFileExists;

	PFN_GETEXTENSIONVERSION		Func_GetExtensionVersion;
	PFN_HTTPEXTENSIONPROC		Func_HttpExtensionProc;
	PFN_TERMINATEEXTENSION		Func_TerminateExtension;

	DllRecord():
		Handle (NULL),
		FreeLibraryAtReload (true),
		CheckThatFileExists (true),
		Func_GetExtensionVersion (NULL),
		Func_HttpExtensionProc (NULL),
		Func_TerminateExtension (NULL)
	{  }

};

struct ConnectionInfo
{
	ahttp::HttpContext* context;
	EXTENSION_CONTROL_BLOCK* controlBlock;
	HANDLE processEvent;

	void* completionFunc;
	void* completionFuncArg;

	ConnectionInfo () :
		context (NULL),
		controlBlock (NULL),
		processEvent (INVALID_HANDLE_VALUE),
		completionFunc (NULL),
		completionFuncArg (NULL)
	{ }
};


namespace Globals
{
	// constants 
	const aconnect::string_constant FuncName_GetExtensionVersion	= "GetExtensionVersion";
	const aconnect::string_constant FuncName_HttpExtensionProc		= "HttpExtensionProc";			
	const aconnect::string_constant FuncName_TerminateExtension		= "TerminateExtension";
	
	const aconnect::string Param_Engine	= "engine";
	const aconnect::string Param_UpdatePath	= "update-path";
	const aconnect::string Param_FreeLibrary	= "free-library";
	const aconnect::string Param_CheckThatFileExists	= "check-file-exists";

	// globals
	boost::mutex LoadMutex;
	std::map<int, DllRecord> RegisteredEnginesMap;

	static ahttp::HttpServerSettings *GlobalServerSettings = NULL;

	
}
HANDLER_EXPORT bool initPlugin  (const aconnect::str2str_map& params, 
								  int handlerIndex,
								  ahttp::HttpServerSettings *globalSettings);
HANDLER_EXPORT void destroyPlugin  ();
HANDLER_EXPORT bool processHandlerRequest (ahttp::HttpContext& context, int handlerIndex);

//////////////////////////////////////////////////////////////////////////
//	
//	ISAPI callbacks

BOOL WINAPI GetServerVariable ( HCONN conn_id, LPSTR variableName, LPVOID buffer, LPDWORD bufferSize);
BOOL WINAPI WriteClient  ( HCONN conn_id, LPVOID buffer, LPDWORD buffSize, DWORD reserved );
BOOL WINAPI ReadClient  ( HCONN conn_id, LPVOID buffer, LPDWORD buffSize);
BOOL WINAPI ServerSupportFunction (	HCONN conn_id, DWORD HSE_code, LPVOID buffer, LPDWORD buffSize, LPDWORD dataType );

//////////////////////////////////////////////////////////////////////////
//	
//	Windows related stuff

BOOL APIENTRY DllMain( HMODULE hModule,
					  DWORD  ul_reason_for_call,
					  LPVOID lpReserved
					  )
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		// Don't need to be called for new threads
		DisableThreadLibraryCalls ((HMODULE) hModule);
	}
	else if (ul_reason_for_call == DLL_PROCESS_DETACH)
	{

	}
	return TRUE;
}

bool updatePath (const aconnect::string& pathUpdate)
{
	char* pathStorage = NULL;
	size_t requiredSize = 0;
	const char* pathVarName = "PATH"; 

	errno_t res = getenv_s( &requiredSize, NULL, 0, pathVarName);
	if (0 != res) {
		Globals::GlobalServerSettings->logger()->error ("Failed to get PATH variable size, error: %d", res);
		return false;
	}

	pathStorage = (char*) malloc(requiredSize * sizeof(char));
	if (!pathStorage) {
		Globals::GlobalServerSettings->logger()->error ("Failed to allocate memory, to modify system PATH");
		return false;
	}

	// Get the value of the PATH environment variable.
	getenv_s( &requiredSize, pathStorage, requiredSize, pathVarName );

	aconnect::str_stream updatedPath;
	
	if( pathStorage) {
		Globals::GlobalServerSettings->logger()->debug ("PATH before update: %s", pathStorage);
		updatedPath << pathStorage << ";";
	}
	
	updatedPath << pathUpdate;

	// Attempt to change path. Note that this only affects
	// the environment variable of the current process. The command
	// processor's environment is not changed.
	res = _putenv_s( pathVarName, updatedPath.str().c_str() );
	if (0 != res) {
		Globals::GlobalServerSettings->logger()->error ("Failed to update PATH variable, error: %d", res);
		return false;
	}

	Globals::GlobalServerSettings->logger()->debug ("PATH after update: %s", updatedPath.str().c_str());

	return true;
}


/* 
*	Handler initialization function - return true if initialization performed suñcessfully
*/
HANDLER_EXPORT bool initPlugin  (const aconnect::str2str_map& params, 
								  int handlerIndex,
								  ahttp::HttpServerSettings *globalSettings)
{
	assert (globalSettings);
	assert (globalSettings->logger());

	boost::mutex::scoped_lock lock(Globals::LoadMutex);
	bool firstRun = true;

	if (Globals::GlobalServerSettings)
		firstRun = false;
	
	if (firstRun) 
	{
		Globals::GlobalServerSettings = globalSettings;
		ahttp::HttpServer::init ( globalSettings ); // should be initialized to correct work

		// prepare context
		HRESULT hr = ::CoInitializeEx(0, COINIT_MULTITHREADED);
		if (FAILED (hr)) {
			globalSettings->logger()->error ("CoInitializeEx call failed, hr: 0x%08X", hr);
			return false;
		}
	}

	DllRecord currentRecord;
	// load ISAPI extension settings
	aconnect::str2str_map::const_iterator it = params.find(Globals::Param_Engine);
	if (it != params.end()) {
		currentRecord.Path = fs::complete (fs::path(it->second, fs::windows_name));

		if (!fs::exists (currentRecord.Path))
		{
			globalSettings->logger()->error ("ISAPI extension DLL not found: %s", 
				currentRecord.Path.file_string().c_str() );
			return false;
		}

	} else {
		globalSettings->logger()->error ("Mandatory parameter '%s' is absent", 
			Globals::Param_Engine.c_str() );
	}

	it = params.find(Globals::Param_UpdatePath);
	if (it != params.end()) 
	{
		if ( !updatePath(it->second))
			return false;
	}

	currentRecord.FreeLibraryAtReload = aconnect::util::getItemFromMapBool (params, Globals::Param_FreeLibrary, true);
	currentRecord.CheckThatFileExists = aconnect::util::getItemFromMapBool (params, Globals::Param_CheckThatFileExists, true);

	// load extension
	aconnect::string dllPath = currentRecord.Path.file_string();

	if ( !updatePath(currentRecord.Path.branch_path().directory_string()) )
		return false;

	// load ISAPI engine
	currentRecord.Handle = ::LoadLibraryA (dllPath.c_str());

	if (NULL == currentRecord.Handle) {
		globalSettings->logger()->error ("ISAPI extension DLL loading failed, library: %s", 
			dllPath.c_str());
		return false;
	}

	currentRecord.Func_GetExtensionVersion = (PFN_GETEXTENSIONVERSION) ::GetProcAddress (currentRecord.Handle, 
		Globals::FuncName_GetExtensionVersion);

	if (!currentRecord.Func_GetExtensionVersion) {
		globalSettings->logger()->error  ("'GetExtensionVersion' function loading failed, "
			"library: %s, error code: %d", dllPath.c_str(), ::GetLastError());
		return false;
	}

	currentRecord.Func_HttpExtensionProc = (PFN_HTTPEXTENSIONPROC) ::GetProcAddress (currentRecord.Handle, 
		Globals::FuncName_HttpExtensionProc);

	if (!currentRecord.Func_HttpExtensionProc) {
		globalSettings->logger()->error  ("'HttpExtensionProc' function loading failed, "
			"library: %s, error code: %d", dllPath.c_str(), ::GetLastError());
		return false;
	}

	currentRecord.Func_TerminateExtension = (PFN_TERMINATEEXTENSION) ::GetProcAddress (currentRecord.Handle, 
		Globals::FuncName_TerminateExtension);

	if (!currentRecord.Func_TerminateExtension) {
		globalSettings->logger()->warn ("'TerminateExtension' function not found, "
			"library: %s, error code: %d", dllPath.c_str(), ::GetLastError());
	}


	HSE_VERSION_INFO isapiVersion = {0,};
	BOOL inited = currentRecord.Func_GetExtensionVersion (&isapiVersion);
	
	if (TRUE != inited)	{
		DWORD errorCode = ::GetLastError();
		globalSettings->logger()->error  ("'GetExtensionVersion' call failed, "
			"library: %s, error code: %d, message: %s", 
				dllPath.c_str(), 
				errorCode,
				aconnect::util::formatWindowsError(errorCode).c_str() );
		return false;

	} else {

		globalSettings->logger()->info ("ISAPI extension loaded, version: %d, description: %s", 
			isapiVersion.dwExtensionVersion, isapiVersion.lpszExtensionDesc);
	}

	Globals::RegisteredEnginesMap[handlerIndex] = currentRecord;

	return true;
}


HANDLER_EXPORT void destroyPlugin  ()
{
	boost::mutex::scoped_lock lock(Globals::LoadMutex);

	if (NULL == Globals::GlobalServerSettings)
		return; // already cleaned
	
	std::map<int, DllRecord>::iterator iter = Globals::RegisteredEnginesMap.begin();
	for (; iter != Globals::RegisteredEnginesMap.end(); ++iter) 
	{
		if (iter->second.Func_TerminateExtension)
			iter->second.Func_TerminateExtension (HSE_TERM_MUST_UNLOAD);
		
		// INVESTIGATE: PHP 5.2.5 ISAPI crash
		if (iter->second.FreeLibraryAtReload) {
			if (!::FreeLibrary (iter->second.Handle)) {
				Globals::GlobalServerSettings->logger()->error  ("ISAPI extension DLL unloading failed, "
					"library: %s, error code: %d", iter->second.Path.file_string().c_str(), ::GetLastError());
			}
		}
		
	}

	::CoUninitialize();
	Globals::GlobalServerSettings = NULL;
}



/* 
*	Main request processing function - return false if request should
*	be processed by other handlers or by server (true: request completed)
*/
HANDLER_EXPORT bool processHandlerRequest (ahttp::HttpContext& context, 
										   int handlerIndex)
{
	using namespace ahttp;
	using namespace aconnect;
	
	if (!context.isClientConnected())
		return true;

	// lock is not needed - RegisteredEnginesMap won't be updated at run
	std::map<int, DllRecord>::const_iterator iter = Globals::RegisteredEnginesMap.find(handlerIndex);
	if (iter == Globals::RegisteredEnginesMap.end())
	{
		HttpServer::processServerError(context, 
					ahttp::HttpStatus::InternalServerError,
					"Invalid handler registration");
		return true;
	}
	
	const DllRecord& currentRecord = iter->second;
		
	if (currentRecord.CheckThatFileExists && !util::fileExists (context.FileSystemPath.string()) ) {
		HttpServer::processError404 (context);
		return true;
	}
	
	ConnectionInfo conInfo;
	


	EXTENSION_CONTROL_BLOCK controlBlock = {0,};
	controlBlock.cbSize = sizeof (EXTENSION_CONTROL_BLOCK);
	controlBlock.dwVersion = 0x500; /* Revision 5.0 */
	
	conInfo.controlBlock = &controlBlock;
	conInfo.context = &context;

	controlBlock.ConnID = (void*) &conInfo;
	controlBlock.dwHttpStatusCode = 0;

	strcpy(controlBlock.lpszLogData, "");

	aconnect::string fileSystemPath (context.FileSystemPath.file_string()),
			contentType(""),
			requestBuffer;

	// these fields will not be updated at current request processing
	controlBlock.lpszMethod = (char *) context.RequestHeader.Method.c_str();
	controlBlock.lpszQueryString = (char *) context.QueryString.c_str();
	controlBlock.lpszPathInfo = (char *) context.VirtualPath.c_str();
	controlBlock.lpszPathTranslated = (char *) fileSystemPath.c_str();
	controlBlock.lpszContentType = (char *) contentType.c_str(); 

	controlBlock.GetServerVariable = GetServerVariable;
	controlBlock.WriteClient = WriteClient;
	controlBlock.ReadClient = ReadClient;
	controlBlock.ServerSupportFunction = ServerSupportFunction;

	// Set up client input
	if (context.RequestHeader.isContentLengthRead())
	{
		if (context.RequestHeader.ContentLength == 0) {
			controlBlock.cbTotalBytes = 0;
			controlBlock.cbAvailable = 0;
			controlBlock.lpbData = NULL; 
		
		} else {
			controlBlock.cbTotalBytes = (DWORD) context.RequestHeader.ContentLength;
			
			if (context.RequestStream.hasBufferedContent() )
			{
				context.RequestStream.giveBuffer(requestBuffer);
				controlBlock.cbAvailable = requestBuffer.size();
				controlBlock.lpbData = (LPBYTE) requestBuffer.c_str();
			}
			else
			{
				controlBlock.cbAvailable = 0;
				controlBlock.lpbData = NULL; 
			}
		}

	} else {
		// CHECK: chunked request support

		controlBlock.cbTotalBytes = 0xffffffff;
		controlBlock.cbAvailable = 0;
	}

	// prepare async. execution
	HANDLE processEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	if (NULL == processEvent)
	{
		DWORD errorCode = ::GetLastError();
		context.Log->error ("[ISAPI::processHandlerRequest]: error async. event creation (%s), code: %d, message: %s", 
			context.VirtualPath.c_str(),
			errorCode,
			aconnect::util::formatWindowsError(errorCode).c_str() );

		HttpServer::processServerError(context, 
					ahttp::HttpStatus::InternalServerError,
					aconnect::util::formatWindowsError(errorCode).c_str());

		return true;
	}
	
	conInfo.processEvent = processEvent;
	
	DWORD rv = currentRecord.Func_HttpExtensionProc(&controlBlock);
	

	switch(rv) 
	{
        case 0:  // Strange, but MS isapi accepts this as success 
        case HSE_STATUS_SUCCESS:
        case HSE_STATUS_SUCCESS_AND_KEEP_CONN:
            //  Per Microsoft: "In IIS versions 4.0 and later, the return
            //  values HSE_STATUS_SUCCESS and HSE_STATUS_SUCCESS_AND_KEEP_CONN
            //  are functionally identical: Keep-Alive connections are
            //  maintained, if supported by the client."
            break;

        case HSE_STATUS_PENDING:
			
             // The completion port was locked prior to invoking HttpExtensionProc().  Once we can regain the lock,
             // when ServerSupportFunction(HSE_REQ_DONE_WITH_SESSION) is called by the extension to release the lock,
             // we may finally destroy the request.
			{
				DWORD res = WaitForSingleObject (processEvent, 
					Globals::GlobalServerSettings->serverSettings().socketWriteTimeout * 1000);
			
				if (res == WAIT_TIMEOUT || res == WAIT_FAILED)
				{
					DWORD errorCode = ::GetLastError();
					context.Log->error ("[ISAPI::processHandlerRequest]: error waiting async. event (%s), code: %d, message: %s", 
						context.VirtualPath.c_str(),
						errorCode,
						aconnect::util::formatWindowsError(errorCode).c_str() );
				}
			}


            break;

        case HSE_STATUS_ERROR:
			context.Log->error ("[ISAPI::processHandlerRequest]: HSE_STATUS_ERROR retrieved at \"%s\" execution", 
					context.VirtualPath.c_str());
		
			HttpServer::processServerError(context, 
					ahttp::HttpStatus::InternalServerError);

            break;

        default:
			context.Log->error ("[ISAPI::processHandlerRequest]: unrecognized result code %d retrieved at \"%s\" execution", 
					rv,
					context.VirtualPath.c_str() );
		
			HttpServer::processServerError(context, 
					ahttp::HttpStatus::InternalServerError);
			break;
    }

	::CloseHandle(processEvent);

	// update Content-Type if not set
	context.setHtmlResponse();



	return true;
}

void processException (ahttp::HttpContext* context, aconnect::string_constptr prefix)
{
	try
	{
		throw;

	} catch (std::exception const &ex)  {
		context->Log->error ("%s: exception caught (%s): %s", 
			prefix, typeid(ex).name(), ex.what());
	
	} catch (...)  {
		context->Log->error ("%s: unknown exception caught", prefix);
		
	}
}

////////////////////////////////////////////////////////////////////////////////
//
//		Callbacks
//

BOOL WINAPI GetServerVariable ( HCONN conn_id, LPSTR variableName, LPVOID buffer, LPDWORD bufferSize)
{
	using namespace aconnect;

	if (NULL == conn_id)
		return FALSE;
	if (NULL == variableName || 0 == bufferSize || 0 == *bufferSize)
		return FALSE;

	ConnectionInfo *conInfo = static_cast<ConnectionInfo*> (conn_id);
	ahttp::HttpContext* context = conInfo->context;

	int initBufferSize = *bufferSize;

	char* bufferData = static_cast<char *> (buffer); 
	bufferData[0] = '\0';
	*bufferSize = 0;

	string result = context->getServerVariable (variableName);
	
	if (!result.empty()) {
		if (0 == strcpy_s (bufferData, initBufferSize, result.c_str()))
			*bufferSize = result.length();
		else
			context->Log->error ("[ISAPI::GetServerVariable]: %s loading failed - buffer too small, target: %s",
				variableName,
				context->VirtualPath.c_str());
	}

	return TRUE;
}

//
//	Main logic was copied from Apache mod_isapi.
//	All code was rewritten thus it cannot be treated as unlawful use, I think )))
//	
BOOL WINAPI ServerSupportFunction (HCONN conn_id, DWORD HSE_code, LPVOID buffer, LPDWORD buffSize, LPDWORD dataType )

{
	using namespace ahttp;

	ConnectionInfo *conInfo = static_cast<ConnectionInfo*> (conn_id);
	ahttp::HttpContext* context = conInfo->context;

	char *bufferData = static_cast<char*> (buffer);
	
    switch (HSE_code) 
	{
    case HSE_REQ_SEND_URL_REDIRECT_RESP:
        // WARNING: Microsoft now advertises HSE_REQ_SEND_URL_REDIRECT_RESP
        //          and HSE_REQ_SEND_URL as equivalant per the Jan 2000 SDK.
        //          They most definitely are not, even in their own samples.
        if (!bufferData)
			return FALSE;

		context->Response.Header.Headers[strings::HeaderLocation] = bufferData;
		context->Response.Header.Status = HttpStatus::Found;
		context->Response.sendHeaders ();
		return TRUE;
    
	case HSE_REQ_SEND_URL:
		// Soak up remaining input 
        context->loadPostParams();

        // Reset the method to GET
		context->Method = HttpMethod::Get;
		context->RequestHeader.Method = strings::HttpMethodGet;
        context->RequestHeader.ContentLength = 0;
        context->RequestHeader.Path = bufferData;
		
		return ahttp::HttpServer::processRequest(*context) ? TRUE : FALSE;

    case HSE_REQ_SEND_RESPONSE_HEADER:
    {
		if (context->Response.isHeadersSent()) {
			context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_SEND_RESPONSE_HEADER)]: headers already sent, target: %s", 
				context->VirtualPath.c_str());
			return FALSE;
		}
        
		try
		{
			context->Response.sendHeadersContent(bufferData, reinterpret_cast<char*> (dataType));
			return TRUE;
		
		} catch (...)  {
			processException (context, "[ISAPI::ServerSupport(HSE_REQ_SEND_RESPONSE_HEADER)] headers sending failed");
		}
		return FALSE;
    }

	case HSE_REQ_SEND_RESPONSE_HEADER_EX:  // Added in ISAPI 4.0
    {
		if (!buffer) {
			context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_MAP_URL_TO_PATH_EX)]: invalid input");
			return FALSE;
		}

		HSE_SEND_HEADER_EX_INFO *info = (HSE_SEND_HEADER_EX_INFO*) buffer;

		if (info->cchHeader == 0 || info->cchHeader == 0) {
			context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_MAP_URL_TO_PATH_EX)]: no data to send");
			return FALSE;
		}

		if (context->Response.isHeadersSent()) {
			context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_SEND_RESPONSE_HEADER_EX)]: headers already sent, target: %s", 
				context->VirtualPath.c_str());
			return FALSE;
		}

		try {
			// Ignore it - PHP always send FALSE - not good
			// if (!info->fKeepConn)
			//	context->Response.Header.setConnectionClose();
			context->Response.sendHeadersContent(info->pszStatus, info->pszHeader);
			return TRUE;
		
		} catch (...)  {
			processException (context, "[ISAPI::ServerSupport(HSE_REQ_SEND_RESPONSE_HEADER_EX)] headers sending failed");
			
		}
		return FALSE;
    }

    case HSE_REQ_DONE_WITH_SESSION:
        // Signal to resume the thread completing this request,
        // leave it to the pool cleanup to dispose of our mutex.
		{
			
			HANDLE processEvent = conInfo->processEvent;
			if (processEvent != INVALID_HANDLE_VALUE) 
			{
				if (!::SetEvent(processEvent)) 
				{
					DWORD errorCode = ::GetLastError();
					context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_DONE_WITH_SESSION)]: error setting async. event (%s), code: %d, message: %s", 
						context->VirtualPath.c_str(),
						errorCode,
						aconnect::util::formatWindowsError(errorCode).c_str() );

					return FALSE;
				}

				return TRUE;

			} else {
				context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_DONE_WITH_SESSION)]: error loading async. event from context(%s)", 
						context->VirtualPath.c_str());
			}
		}
        return FALSE;

    case HSE_REQ_MAP_URL_TO_PATH:
    {
        // Map a virtual path
		if (!bufferData) {
			context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_MAP_URL_TO_PATH)]: invalid input");
			return FALSE;
		}

        // Map a URL to a filename 
		bool fileExists = false;
        string testUri( bufferData, *buffSize);
		string realPath = context->mapPath (testUri.c_str(), fileExists);
		
		if ((DWORD) realPath.size() <  *buffSize) {
			::SetLastError (ERROR_INSUFFICIENT_BUFFER);
			return FALSE;
		}

		strcpy (bufferData, realPath.c_str());
		*buffSize = (DWORD) realPath.size();
        
        return TRUE;
    }
	
	case HSE_REQ_MAP_URL_TO_PATH_EX:
    {
		if (!bufferData || !dataType ) {
			context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_MAP_URL_TO_PATH_EX)]: invalid input");
			return FALSE;
		}

        // Map a URL to a filename 
        HSE_URL_MAPEX_INFO *info = (HSE_URL_MAPEX_INFO*) dataType;
        string testUri( bufferData, *buffSize);

		bool fileExists = false;
		string realPath = context->mapPath (testUri.c_str(), fileExists);
		aconnect::util::zeroMemory( (void*)info, sizeof(HSE_URL_MAPEX_INFO));
		
        info->cchMatchingURL = testUri.size();
		info->cchMatchingPath = aconnect::util::min2( (int) realPath.size(), MAX_PATH);
		strcpy (info->lpszPath, realPath.c_str());

		if (fileExists)
		{
			DWORD attr = ::GetFileAttributesA (realPath.c_str());

			if ((attr & FILE_ATTRIBUTE_HIDDEN) == 0
				&& (attr & FILE_ATTRIBUTE_SYSTEM) == 0)
			{
				info->dwFlags |= HSE_URL_FLAGS_READ 
					| HSE_URL_FLAGS_EXECUTE 
					| HSE_URL_FLAGS_SCRIPT;
				
				if ((attr & FILE_ATTRIBUTE_READONLY) == 0)
					info->dwFlags |= HSE_URL_FLAGS_WRITE;
			}
		}
	
        return TRUE;
    }
	
	case HSE_REQ_IS_KEEP_CONN:
		if (!buffer) {
			context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_IS_KEEP_CONN)]: invalid input");
			return FALSE;
		}

		*((int *) buffer) = (context->IsKeepAliveConnect ? TRUE : FALSE); // convert to BOOL
		return TRUE;

	case HSE_REQ_IS_CONNECTED:  // Added after ISAPI 4.0 
		if (!buffer) {
			context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_IS_CONNECTED)]: invalid input");
			return FALSE;
		}
		
		// Returns True if client is connected
		*((int *) buffer) = (context->isClientConnected() ? TRUE : FALSE);
		return TRUE;

	case HSE_APPEND_LOG_PARAMETER:
		// Log buffer, of buffSize bytes
		if (!bufferData) {
			context->Log->error ("[ISAPI::ServerSupport(HSE_APPEND_LOG_PARAMETER)]: invalid input");
			return FALSE;
		}

		context->Log->info ("[ISAPI::Log]: %s, URL: %s",
			bufferData, context->VirtualPath.c_str());
		return TRUE;

	case HSE_REQ_CLOSE_CONNECTION:
		context->closeConnection (true);
		return TRUE;

  
	case HSE_REQ_GET_SSPI_INFO:
		context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_GET_SSPI_INFO)]: this option is not supported");
		return FALSE;
	case HSE_REQ_REFRESH_ISAPI_ACL:
		context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_REFRESH_ISAPI_ACL)]: this option is not supported");
		return FALSE;
	case HSE_REQ_GET_IMPERSONATION_TOKEN:  // Added in ISAPI 4.0
		context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_GET_IMPERSONATION_TOKEN)]: this option is not supported");
		return FALSE;
	case HSE_REQ_ABORTIVE_CLOSE:
		context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_ABORTIVE_CLOSE)]: this option is not supported");
		return FALSE;
    case HSE_REQ_GET_CERT_INFO_EX:
		context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_GET_CERT_INFO_EX)]: this option is not supported");
		return FALSE;
	case HSE_REQ_EXTENSION_TRIGGER:
        context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_EXTENSION_TRIGGER)]: this option is not supported");
		return FALSE;
	
	case HSE_REQ_IO_COMPLETION:
		if (NULL == bufferData) {
			context->Log->error ("[ISAPI::ServerSupport(HSE_REQ_IO_COMPLETION)]: invalid input");
			return FALSE;
		}

		conInfo->completionFunc = reinterpret_cast<void*> (bufferData);
		conInfo->completionFuncArg = reinterpret_cast<void*> (dataType);

		return TRUE;
	
	case HSE_REQ_TRANSMIT_FILE:
    case HSE_REQ_ASYNC_READ_CLIENT:
		 // TODO: implement async. operations

	default:
		context->Log->error ("ISAPI: ServerSupportFunction (%d) not supported: %s", HSE_code, 
			context->VirtualPath.c_str());
        return FALSE;
    }
	
	return FALSE;
}


BOOL WINAPI WriteClient  ( HCONN conn_id, LPVOID buffer, LPDWORD buffSize, DWORD reserved )
{
	if (NULL == conn_id || NULL == buffer)
		return FALSE;

	ConnectionInfo *conInfo = static_cast<ConnectionInfo*> (conn_id);
	ahttp::HttpContext* context = conInfo->context;

	void* callbackArg = NULL;
	EXTENSION_CONTROL_BLOCK* ecb = NULL;
	PFN_HSE_IO_COMPLETION callback = NULL;

	callback = reinterpret_cast<PFN_HSE_IO_COMPLETION> (conInfo->completionFunc);
	
	if (callback) {
		callbackArg = conInfo->completionFuncArg;
		ecb = conInfo->controlBlock;

		assert (ecb);
	}

	try
	{
		context->Response.write ((char *) buffer, (size_t) *buffSize);
		
		if (callback)
			callback (ecb, callbackArg, *buffSize, ERROR_SUCCESS);
		return TRUE;
	
	} catch (...)  {
		processException (context, "[ISAPI::WriteClient] Data writing failed");
		
	}

	if (callback)
		callback (ecb, callbackArg, *buffSize, ERROR_WRITE_FAULT);
	return FALSE;
}

BOOL WINAPI ReadClient  (HCONN conn_id, LPVOID buffer, LPDWORD buffSize)
{
	if (NULL == conn_id || NULL == buffer || NULL == buffSize || 0 == *buffSize)
		return FALSE;

	ConnectionInfo *conInfo = static_cast<ConnectionInfo*> (conn_id);
	ahttp::HttpContext* context = conInfo->context;
	
	if (context->RequestStream.isRead())
		return FALSE;

	try
	{
		int read = context->RequestStream.read( static_cast<char*> (buffer), * buffSize);
		*buffSize = read;
		
		return TRUE;

	} catch (...)  {
		processException (context, "[ISAPI::ReadClient] Data reading failed");
	}
	return FALSE;
}


#endif // WIN32
