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

// python_handler.cpp : Defines the entry point for the DLL application.
//

#include <boost/algorithm/string.hpp>
#include <boost/thread.hpp>
#include <boost/filesystem.hpp>

#include "ahttplib.hpp"
#include "aconnect/util.hpp"
#include "aconnect/util.string.hpp"
#include "aconnect/util.file.hpp"

namespace algo = boost::algorithm;
namespace fs = boost::filesystem;

#include "wrappers.hpp"
#include "module.inl"

namespace Globals
{
	// constants
	const aconnect::string Param_UploadsDir = "uploads-dir";
	
	// globals
	boost::mutex LoadMutex;
    aconnect::string UploadsDirPath;

	static ahttp::HttpServerSettings *GlobalServerSettings = NULL;

	PyThreadState * MainThreadState = NULL;
	python::object	MainModule;
	python::dict	MainModuleDict;
}


HANDLER_EXPORT bool initPlugin  (const aconnect::str2str_map& params, int handlerIndex,
								  ahttp::HttpServerSettings *globalSettings);
HANDLER_EXPORT void destroyPlugin ();
HANDLER_EXPORT bool processHandlerRequest (ahttp::HttpContext& context, int handlerIndex);


void executeScript (aconnect::string_constref scriptPath, ahttp::HttpContext& context, PyThreadState *ts);


//////////////////////////////////////////////////////////////////////////
//	
//	Windows related stuff
#if defined (WIN32)
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
#endif



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
    
    if (Globals::GlobalServerSettings) {
		globalSettings->logger()->error ("Handler already initialized - python handler cannot be loaded twice");
		return false;
	}
	
	// load settings
	aconnect::str2str_map::const_iterator it = params.find(Globals::Param_UploadsDir);
	
    if (it != params.end()) {
		Globals::UploadsDirPath = it->second;
		if (!fs::exists (Globals::UploadsDirPath)) {

			try {
				fs::create_directories(Globals::UploadsDirPath);
			} catch (...) {
				globalSettings->logger()->error ("Python handler: uploads directory creation failed", 
					Globals::Param_UploadsDir.c_str() );
				
				return false;
			}
		} 
	} 

	ahttp::HttpServer::init ( globalSettings ); // should be initialized to correct work

	Globals::GlobalServerSettings = globalSettings;

	// Init Python interpreter in multithreaded mode
	Py_Initialize ();

	if ( 0 == Py_IsInitialized()) {
		globalSettings->logger()->error ("Python interpreter was not initialized correctly");
		return false;
	}

	PyEval_InitThreads();

	PyObject *serverPath = PyString_FromString (globalSettings->appLocaton().c_str());
	PySys_SetObject("executable", serverPath);
	Py_DecRef(serverPath);


	// MT support
	// save a pointer to the main PyThreadState object
	Globals::MainThreadState = PyThreadState_Get();
	// release the lock
	PyEval_ReleaseLock();

	// Register the module with the interpreter
	if (PyImport_AppendInittab("python_handler", initpython_handler) == -1)
	{
		globalSettings->logger()->error ("Failed to register 'python_handler' in the interpreter's built-in modules");
		return false;
	}

	// Retrieve the main module's namespace
	Globals::MainModule = python::import("__main__");
	Globals::MainModuleDict = python::dict (Globals::MainModule.attr("__dict__"));
	// register classes
	python::import ("python_handler");
	

	return true;
}


HANDLER_EXPORT void destroyPlugin  ()
{
	boost::mutex::scoped_lock lock(Globals::LoadMutex);

	if ( Py_IsInitialized() )
	{
		PyEval_AcquireLock();

		Py_DECREF(Globals::MainModule.ptr());
				
		PyThreadState_Swap(Globals::MainThreadState );
		
		// Py_Finalize();
	}

	Globals::MainThreadState = NULL;
	Globals::GlobalServerSettings = NULL;
}

void releasePythodThread (PyThreadState *ts) 
{
	if (ts)
	{
		// PyEval_ReleaseLock();
		// PyEval_AcquireLock();

		// clear the thread state
		// swap my thread state out of the interpreter
		ts = PyThreadState_Swap(NULL);
		
		if (ts) 
		{
			PyThreadState_Clear(ts);
			PyThreadState_Delete(ts);
		}
		
	}
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
	
	if ( !util::fileExists (context.FileSystemPath.string()) ) {
		HttpServer::processError404 (context);
		return true;
	}

	if (!context.isClientConnected())
		return true;

	
	// init context
	if (!Globals::UploadsDirPath.empty())
		context.UploadsDirPath = Globals::UploadsDirPath;
	

	PyThreadState *ts = NULL;
	
	ScopedGuard<simple_callback>  guard (PyEval_ReleaseLock);
	ScopedParamGuard < void (*) (PyThreadState *), PyThreadState*> guardTs (releasePythodThread, ts);
	
	try 
	{

		// get the global lock
		PyEval_AcquireLock();
		// get a reference to the PyInterpreterState
		PyInterpreterState * mainInterpreterState = Globals::MainThreadState->interp;

		// create a thread state object for this thread
		ts = PyThreadState_New (mainInterpreterState);
		PyEval_ReleaseLock();

		if ( !ts )
			throw std::bad_alloc();
		
		PyEval_AcquireLock();
		PyThreadState_Swap(ts);

        executeScript (context.FileSystemPath.file_string(), context, ts);
		
		context.setHtmlResponse();

	} catch (python::error_already_set const &)  {
		
		try
		{
			string errDesc = loadPythonError ();
			context.Log->errorSafe(errDesc.c_str());
			
			// prepare HTML response
			errDesc = util::escapeHtml (errDesc);
			algo::replace_all (errDesc, "\n", "<br />");
			algo::replace_all (errDesc, "  ", "&nbsp;&nbsp;");
			HttpServer::processServerError(context, 500, errDesc.c_str() );
		}
		catch (...)
		{
			context.Log->error("Python handler: loading Python error failed, file: %s", 
				context.FileSystemPath.string().c_str());
		}

	} catch (std::exception const &ex)  {
		context.Log->error ("Python: exception caught (%s): %s, file: %s", 
			typeid(ex).name(), ex.what(),
			context.FileSystemPath.string().c_str());
		
		throw;
			
	} catch (...)  {
		context.Log->error ("Python: unknown exception caught, file: %s",
			context.FileSystemPath.string().c_str());
		
		HttpServer::processServerError(context, 500);
	}
	
	

	return true;
}



/**
 * Execute python code 
 */
void executeScript (aconnect::string_constref scriptPath, ahttp::HttpContext& context, PyThreadState *ts )
{
	using namespace python;
	
	HttpContextWrapper wrapper (&context);
	
	dict global (Globals::MainModuleDict.copy ());
	dict local (Globals::MainModuleDict.copy ());
	
	// prepare globals
	reference_existing_object::apply<HttpContextWrapper*>::type converter;
	handle<> wrapperHandle ( converter( &wrapper ) );
	
	local["http_context"] = wrapperHandle;
	
	/*
		Wrong way to redefine globals ('write' method will be used)
		PySys_SetObject("stdout", wrapperHandle.get());	
		PySys_SetObject("stderr", wrapperHandle.get());	
	*/

	PyObject *pyfile = PyFile_FromString (const_cast<char*>(scriptPath.c_str()), "r");
	python::handle<> file (pyfile);
	FILE* fptr = PyFile_AsFile(file.get());
	
	PyObject* result = PyRun_File (fptr,
		scriptPath.c_str(),
		Py_file_input,
		global.ptr(), 
		local.ptr());
	
	if (!result) 
		throw_error_already_set();
}
