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

#ifndef BASIC_AUTH_PROVIDER_SERVER_H
#define BASIC_AUTH_PROVIDER_SERVER_H

#include "aconnect/complex_types.hpp"
#include "aconnect/error.hpp"
#include "aconnect/logger.hpp"

#include "aconnect/crypto/hasher.hpp"
#include "aconnect/crypto/password_file_storage.hpp"

#include "ahttp/common/auth_provider.hpp"
#include "ahttp/http_server_settings.hpp"

class ServerAuthenticationProvider : public ahttp::auth::AuthenticationProvider
{
public:
	ServerAuthenticationProvider (const aconnect::str2str_map& params, ahttp::HttpServerSettings *globalSettings)
		throw (aconnect::application_error);

	ahttp::auth::AuthenticationResult authenticate (aconnect::string_constref userName, 
				aconnect::string_constref password);

protected:
	aconnect::Logger* _log;
	std::auto_ptr<aconnect::crypto::Hasher> _hasher;
	aconnect::crypto::PasswordFileStorage _passwordsStore;
};

#endif // BASIC_AUTH_PROVIDER_SERVER_H


