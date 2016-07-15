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

#include "auth_provider_server.hpp"

namespace
{
	aconnect::string_constant Param_UsersFile = "users-file";
	aconnect::string_constant Param_HashAlgorithm = "hash-algorithm";
	aconnect::string_constant Param_HashSalt = "hash-salt";
}


ServerAuthenticationProvider::ServerAuthenticationProvider (const aconnect::str2str_map& params,
			ahttp::HttpServerSettings *globalSettings) throw (aconnect::application_error)
			: _hasher(NULL)

{
	assert (globalSettings && globalSettings->logger());
	_log = globalSettings->logger();


	using namespace aconnect::crypto;

	aconnect::string filePath = aconnect::util::getItemFromMap (params, Param_UsersFile);
	globalSettings->updateAppLocationInPath (filePath);

	_passwordsStore.init (filePath);	

	const aconnect::string hashAlgo = aconnect::util::getItemFromMap (params, Param_HashAlgorithm);
	const aconnect::string hashSalt = aconnect::util::getItemFromMap (params, Param_HashSalt);

	if (!hashAlgo.empty()) {
		Hasher* hasher = createHasher (hashAlgo.c_str(), hashSalt.c_str()); 
		
		if (NULL == hasher)
			throw aconnect::application_error ("[%s] algorithm is not supported", hashAlgo.c_str());
		
		_hasher.reset(hasher);
	}
	
}

ahttp::auth::AuthenticationResult ServerAuthenticationProvider::authenticate (aconnect::string_constref userName, 
				aconnect::string_constref password)
{
	using namespace ahttp::auth;
	using namespace aconnect::crypto;

	PasswordCheckResult res = PasswordCheckInvalid;
	try {

		if (_hasher.get()) 
			res = _passwordsStore.passwordValid (userName, _hasher->getHash(password));
		else
			res = _passwordsStore.passwordValid (userName, password);

		if (res == PasswordCheckUserNotFound)
			return AuthUserNotFound;
	
	} catch (std::exception &ex) {
		
		_log->error (ex);
		return AuthError;
	}

	return (res == PasswordCheckValid ? AuthAccessGranted : AuthAccessDenied);
}

