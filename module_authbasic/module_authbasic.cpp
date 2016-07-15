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

#include <boost/thread.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

#include "ahttplib.hpp"

#include "aconnect/util.hpp"
#include "aconnect/util.string.hpp"
#include "aconnect/util.file.hpp"
#include "aconnect/thirdparty/base64.hpp"

#include "auth_provider_system.hpp"
#include "auth_provider_server.hpp"


namespace
{
	enum ProviderType
	{
		ProviderEmpty,
		ProviderSystem,
		ProviderServer
	};

	struct ModuleConfig
	{
		ProviderType providerType;
		aconnect::string realm;
		mutable std::auto_ptr<AuthenticationProvider> provider;

		ModuleConfig() :			
			providerType (ProviderEmpty),
			provider (NULL)
		{  }

#if defined (WIN32)
		explicit 
#endif
		ModuleConfig (const ModuleConfig& other) 
		{
			assert (provider.get() == NULL);

			providerType = other.providerType;
			realm = other.realm;
			provider = other.provider;
		}
	};
}
namespace Globals
{
	boost::mutex LoadMutex;
	static ahttp::HttpServerSettings *GlobalServerSettings = NULL;
	std::map<int, ModuleConfig> RegisteredConfigMap;


	// params
	const aconnect::string Param_Realm = "realm";
	const aconnect::string Param_Provider = "provider";
	const aconnect::string Param_ProviderSystem = "system";
	const aconnect::string Param_ProviderServer = "server"; // iternal server authentication

	// constants
	const aconnect::string BasicAutenticationKey = "Basic";
	
	const aconnect::string BasicAutenticationHeaderFormat = "Basic Realm=\"<realm>\"";
	const aconnect::string HeaderRealmKey = "<realm>";
}

HANDLER_EXPORT bool initPlugin  (const aconnect::str2str_map& params, 
								 int moduleIndex,
								 ahttp::HttpServerSettings *globalSettings);
HANDLER_EXPORT void destroyPlugin  ();

//////////////////////////////////////////////////////////////////////////
//
//  Each module callbacks could returns 'true' to skip 
//	following module callbacks calls

HANDLER_EXPORT bool onRequestResolve (ahttp::HttpContext& context, int moduleIndex);



//////////////////////////////////////////////////////////////////////////
//
//		Helpers
//
ProviderType loadProviderType (aconnect::string providerName);

AuthenticationProvider* loadProvider (ProviderType type, const aconnect::str2str_map& params, ahttp::HttpServerSettings *globalSettings);

void writeAccessDenied (ahttp::HttpContext& context, const ModuleConfig& config);

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
	
	return TRUE;
}
#endif

/* 
*	Module initialization function - return true if initialization performed suñcessfully
*/
HANDLER_EXPORT bool initPlugin  (const aconnect::str2str_map& params, 
								  int moduleIndex,
								  ahttp::HttpServerSettings *globalSettings)
{
	assert (globalSettings && globalSettings->logger());
	
    boost::mutex::scoped_lock lock(Globals::LoadMutex);
	
    // fisrt run
	if (!Globals::GlobalServerSettings) 
	{
		Globals::GlobalServerSettings = globalSettings;
		ahttp::HttpServer::init ( globalSettings ); // should be initialized to correct work
	}

	aconnect::string providerName = aconnect::util::getItemFromMap (params, Globals::Param_Provider);
	ProviderType providerType = loadProviderType (providerName);

	if (providerType == ProviderEmpty) {
		globalSettings->logger()->error ("Invalid authentication provider type: %s", 
			providerName.c_str());

		return false;
	}


	try
	{

		ModuleConfig configInfo;

		configInfo.realm = aconnect::util::getItemFromMap (params, Globals::Param_Realm, 
			globalSettings->serverVersion().c_str());
		configInfo.providerType = providerType;
		configInfo.provider.reset ( loadProvider (providerType, params, globalSettings) );

		if (configInfo.provider.get() == NULL) {
			globalSettings->logger()->error ("Authentication provider cannot be loaded: %s", 
				providerName.c_str());
			
			return false;
		}

		Globals::RegisteredConfigMap [moduleIndex] = configInfo;
	}
	catch (std::exception &ex)
	{
		globalSettings->logger()->error ("Authentication provider initialization failed: %s", 
				ex.what());
		return false;
	}
		 
	

	return true;
}


HANDLER_EXPORT void destroyPlugin  ()
{
	boost::mutex::scoped_lock lock(Globals::LoadMutex);

	if (NULL == Globals::GlobalServerSettings)
		return; // already cleaned
	
	Globals::RegisteredConfigMap.clear();
	Globals::GlobalServerSettings = NULL;
};


HANDLER_EXPORT bool onRequestResolve (ahttp::HttpContext& context, int moduleIndex)
{
	using namespace ahttp;

	std::map<int, ModuleConfig>::const_iterator cgfIter = Globals::RegisteredConfigMap.find (moduleIndex);

	assert (cgfIter != Globals::RegisteredConfigMap.end() 
		&& "Module was not initialized correctly");

	const ModuleConfig& config = cgfIter->second;

	if (!context.RequestHeader.hasHeader (strings::HeaderAuthorization)) {
		writeAccessDenied (context, config);
		return true;
	}

	aconnect::string auth = context.RequestHeader.getHeader (strings::HeaderAuthorization);
	if (auth.find(Globals::BasicAutenticationKey) != 0) {
		writeAccessDenied (context, config);
		return true;
	}

	auth = auth.substr ( Globals::BasicAutenticationKey.size());
	boost::algorithm::trim (auth);

	auth = aconnect::thirdparty::Base64::decode (auth);

	aconnect::string::size_type delimPos = auth.find (":");
	aconnect::string userName, pass;

	userName = auth.substr (0, delimPos);
	pass = auth.substr (delimPos + 1);
	
	auth::AuthenticationResult res = config.provider->authenticate (userName, pass);
	
	if (res != auth::AuthAccessGranted) 
	{
		writeAccessDenied (context, config);
		return true;
	}

	// Fill following serverVariables
	// AUTH_PASSWORD: <password>
	// AUTH_TYPE: Basic
	// AUTH_USER: <user-name>

	context.Items[strings::ServerVariables::AUTH_TYPE] = Globals::BasicAutenticationKey;
	context.Items[strings::ServerVariables::AUTH_USER] = userName;
	context.Items[strings::ServerVariables::AUTH_PASSWORD] = pass; // maybe unsecure ?
	
	return false;
};

void writeAccessDenied (ahttp::HttpContext& context, const ModuleConfig& config)
{
	context.Response.Header.Status = ahttp::HttpStatus::Unauthorized; // 401
	context.Response.Header.setHeader (ahttp::strings::HeaderWWWAuthenticate, 
		boost::algorithm::replace_all_copy (Globals::BasicAutenticationHeaderFormat,
			Globals::HeaderRealmKey, config.realm));

	aconnect::string errorResponse = ahttp::HttpResponse::getErrorResponse (
			context.Response.Header.Status,
			*Globals::GlobalServerSettings,
			Globals::GlobalServerSettings->getMessage("Error401").c_str());
		
	context.Response.writeCompleteHtmlResponse (errorResponse);
}

////////////////////////////////////////////////////////////////////////////////

ProviderType loadProviderType (aconnect::string providerName)
{
	if (aconnect::util::equals (providerName, Globals::Param_ProviderSystem))
		return ProviderSystem;
	else if (aconnect::util::equals (providerName, Globals::Param_ProviderServer))
		return ProviderServer;

	return ProviderEmpty;
};

AuthenticationProvider* loadProvider (ProviderType type, const aconnect::str2str_map& params, ahttp::HttpServerSettings *globalSettings)
{
	if (type == ProviderSystem)
		return new SystemAuthenticationProvider (params, globalSettings);
	else if (type == ProviderServer)
		return new ServerAuthenticationProvider (params, globalSettings);

	return NULL;

}

////////////////////////////////////////////////////////////////////////////////
