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

#include "aconnect/util.hpp"
#include "aconnect/util.string.hpp"

#include "auth_provider_system.hpp"

namespace
{
	aconnect::string_constant Param_DefaultDomain = "default-domain";
}

SystemAuthenticationProvider::SystemAuthenticationProvider (const aconnect::str2str_map& params,
															ahttp::HttpServerSettings *globalSettings) 
{
	assert (globalSettings && globalSettings->logger());
	_log = globalSettings->logger();

	_defaultDomain = aconnect::util::getItemFromMap (params, Param_DefaultDomain);
}

AuthenticationResult SystemAuthenticationProvider::authenticate (aconnect::string_constref fullUserName, 
				aconnect::string_constref password)
{
	using namespace aconnect;

	if (fullUserName.empty()) {
		_log->error ("System authentication failed, empty user name provided");
		return AuthError;
	}


	string userName, domain;
	string::size_type pos;

	
	if ((pos = fullUserName.find ('@')) != string::npos) {
		userName = fullUserName.substr (0, pos);
		domain = fullUserName.substr (pos + 1);
	
	} else if ((pos = fullUserName.find ('\\')) != string::npos) {
		domain = fullUserName.substr (0, pos);
		userName = fullUserName.substr (pos + 1);

	} else {
		userName = fullUserName;
	}

#if defined (WIN32)
	

	HANDLE userHandle = NULL;

	BOOL rv = ::LogonUserA (userName.c_str(),
			domain.empty() ? NULL : domain.c_str(),	
			password.c_str(),
			LOGON32_LOGON_NETWORK_CLEARTEXT,
			LOGON32_PROVIDER_DEFAULT,
			&userHandle);

	// try to login with default domain
	if (!rv && !_defaultDomain.empty() && domain.empty())
		 rv = ::LogonUserA (userName.c_str(),
			_defaultDomain.c_str(),	
			password.c_str(),
			LOGON32_LOGON_NETWORK_CLEARTEXT,
			LOGON32_PROVIDER_DEFAULT,
			&userHandle);

	if (!rv) 
	{
		DWORD err = GetLastError(); // thread safe
		if (_log->isInfoEnabled())
			_log->info ("System authentication failed, user: %s, error: %s", 
				userName.c_str(), aconnect::util::formatWindowsError (err).c_str());

		return AuthAccessDenied;
	}
	else
	{
		CloseHandle (userHandle);
	}
	
#elif defined (__GNUC__)

	_log->info ("System authentication is not supported");
	return AuthError;

#endif

	return AuthAccessGranted;
}

