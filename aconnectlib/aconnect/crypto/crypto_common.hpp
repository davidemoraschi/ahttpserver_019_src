/*
This file is part of [aconnect] library. 

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

#ifndef ACONNECT_CRYPTO_COMMON_H
#define ACONNECT_CRYPTO_COMMON_H

#include "aconnect/types.hpp"

namespace aconnect { namespace crypto { namespace consts {

	aconnect::string_constant HashAlgorithm_Sha1 = "sha1";
	aconnect::string_constant HashAlgorithm_HmacSha1 = "hmac-sha1";

}}};

#endif // ACONNECT_CRYPTO_COMMON_H


