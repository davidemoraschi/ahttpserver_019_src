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

#ifdef WIN32

#pragma unmanaged

#include <boost/filesystem.hpp>
#include <boost/scoped_array.hpp>
#include <boost/algorithm/string.hpp>

#include "ahttplib.hpp"
#include "aconnect/util.hpp"
#include "aconnect/util.string.hpp"
#include "aconnect/util.file.hpp"

#pragma managed

#include "vcclr.h"

namespace fs = boost::filesystem;
namespace algo = boost::algorithm;

using namespace System;
using namespace System::IO;
using namespace System::Threading;
using namespace System::Collections::Generic;

using namespace AHttp;

namespace Globals
{
	// constants
	const aconnect::string Param_InitRoot = "init-root";
	const aconnect::string Param_LoadApplications = "load-applications";

	// objects
	boost::mutex LoadMutex;
	static ahttp::HttpServerSettings *GlobalServerSettings = NULL;
}

#include "../shared/managed_strings_conversions.inl"
#include "managed.inl"

HANDLER_EXPORT bool initPlugin  (const aconnect::str2str_map& params, 
								  int handlerIndex,
								  ahttp::HttpServerSettings *globalSettings);
HANDLER_EXPORT void destroyPlugin  ();
HANDLER_EXPORT bool processHandlerRequest (ahttp::HttpContext& context, int handlerIndex);


void processException (aconnect::string_constptr prefix);

//////////////////////////////////////////////////////////////////////////

#pragma unmanaged

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

#pragma managed

/* 
*	Handler initialization function - return true if initialization performed suñcessfully
*/
HANDLER_EXPORT bool initPlugin (const aconnect::str2str_map& params, 
								  int handlerIndex,
								  ahttp::HttpServerSettings *globalSettings)
{
	assert (globalSettings);
	assert (globalSettings->logger());

	boost::mutex::scoped_lock lock(Globals::LoadMutex);
	
	if (Globals::GlobalServerSettings) {
		globalSettings->logger()->error ("Handler already initialized - ASP.NET handler cannot be loaded twice");
		return false;
	}


	Globals::GlobalServerSettings = globalSettings;
	ahttp::HttpServer::init ( globalSettings ); // should be initialized to correct work

	try
	{
		if (aconnect::util::getItemFromMapBool (params, Globals::Param_InitRoot, true)) 
		{
			const ahttp::DirectorySettings& root = Globals::GlobalServerSettings->getRootDirSettings ();
			AHttpInternal::Application::InitHost (root);
		}
		
		// process "load-applications" parameter
		aconnect::string loadApps = aconnect::util::getItemFromMap (params, Globals::Param_LoadApplications);
		if (!loadApps.empty()) 
		{
			aconnect::SimpleTokenizer tokens(loadApps, ";");
			aconnect::SimpleTokenizer::iterator tok_iter;
		
			for (tok_iter = tokens.begin(); tok_iter != tokens.end(); ++tok_iter)
			{
				aconnect::string token = tok_iter.current_token();
				algo::trim (token);

				if (token.empty())
					continue;

				const ahttp::DirectorySettings* dir = Globals::GlobalServerSettings->getDirSettingsByName (token);
				if (NULL == dir) {
					globalSettings->logger()->error ("Directory settings not found by name \"%s\"", token.c_str());
					throw std::runtime_error ("Directory settings not found");
				}

				AHttpInternal::Application::InitHost (*dir);
			}
		}
		
	
	} catch (System::Exception^ ex) {
		Globals::GlobalServerSettings->logger()->error ("ASPNET, Initialization: managed exception caught: %s", 
			AHttp::getUnmanagedString(ex->ToString()).c_str());
		return false;

	} catch (...) {
		processException("Initialization");
		return false;
	}

	return true;
}


HANDLER_EXPORT void destroyPlugin  ()
{
	boost::mutex::scoped_lock lock(Globals::LoadMutex);

	if (NULL == Globals::GlobalServerSettings)
		return; // already cleaned

	for each ( KeyValuePair <int, AHttp::AspNetHost^> pair in AHttpInternal::Application::Hosts )
		AppDomain::Unload (pair.Value->GetDomain());

	AHttpInternal::Application::Hosts->Clear();
	
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
	

	try
	{
		AHttp::AspNetHost^ host = nullptr;

		try
		{
			System::Threading::Monitor::Enter (AHttpInternal::Application::typeid);
			
			if ( AHttpInternal::Application::Hosts->ContainsKey(context.CurrentDirectoryInfo->number) ) 
			{
				try {
					host = AHttpInternal::Application::Hosts[context.CurrentDirectoryInfo->number];
					host->GetDomain();

				} catch (AppDomainUnloadedException^) {
					context.Log->warn ("ASPNET: application domain was unloaded, directory: %s",
						context.CurrentDirectoryInfo->name.c_str());
					
					AHttpInternal::Application::Hosts->Remove(context.CurrentDirectoryInfo->number);
					host = nullptr;
				}
			}
			
			if (host == nullptr)
				host = AHttpInternal::Application::InitHost (*context.CurrentDirectoryInfo); 
		} 
		finally
		{
			System::Threading::Monitor::Exit (AHttpInternal::Application::typeid);
		}
		       
		
		// give request to HttpRuntime
		host->ProcessRequest((void*) &context);
		
		
		// update Content-Type if not set
		if (!context.isClosed())
			context.setHtmlResponse();
	
	} catch (System::Exception^ ex) {
		Globals::GlobalServerSettings->logger()->error ("ASPNET, request processing: managed exception caught: %s", 
			AHttp::getUnmanagedString(ex->ToString()).c_str());
	
	} catch (...) {
		processException("Processing request");
	}



	
	return true;
}

void processException (aconnect::string_constptr prefix)
{
	try
	{
		throw;

	} catch (std::exception const &ex)  {
		Globals::GlobalServerSettings->logger()->error ("ASPNET, %s: exception caught (%s): %s", 
			prefix, typeid(ex).name(), ex.what());
	
	} catch (...)  {
		Globals::GlobalServerSettings->logger()->error ("ASPNET, %s: unknown exception caught", prefix);
		
	}
}



////////////////////////////////////////////////////////////////////////////////

#endif // WIN32

