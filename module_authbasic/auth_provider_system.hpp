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

#ifndef BASIC_AUTH_PROVIDER_SYSTEM_H
#define BASIC_AUTH_PROVIDER_SYSTEM_H

#include "aconnect/complex_types.hpp"
#include "aconnect/logger.hpp"

#include "ahttp/common/auth_provider.hpp"
#include "ahttp/http_server_settings.hpp"

using namespace ahttp::auth;

class SystemAuthenticationProvider : public ahttp::auth::AuthenticationProvider
{
public:
	SystemAuthenticationProvider (const aconnect::str2str_map& params, ahttp::HttpServerSettings *globalSettings);

	AuthenticationResult authenticate (aconnect::string_constref fullUserName, 
				aconnect::string_constref password);

protected:
	aconnect::string _defaultDomain;
	aconnect::Logger* _log;
};

#endif // BASIC_AUTH_PROVIDER_SYSTEM_H


