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

#ifndef ACONNECT_CRYPTO_HASHER_H
#define ACONNECT_CRYPTO_HASHER_H

#include "aconnect/util.string.hpp"

#include "aconnect/crypto/crypto_common.hpp"
#include "aconnect/crypto/sha1.hpp"
#include "aconnect/crypto/hmac-sha1.hpp"

namespace aconnect { namespace crypto {

class Hasher
{
public:
	virtual string getHash (string_constref data) = 0;
	virtual ~Hasher() {}
};

class Sha1Hasher : public Hasher
{
	aconnect::crypto::sha1 hasher;

public:
	virtual string getHash (string_constref data) {
		return hasher.process (data);
	}
};

class HmacSha1Hasher : public Hasher
{
	string _key;

public:
	HmacSha1Hasher (string_constref key):
		_key (key) { }

	virtual string getHash (string_constref data) {
		return aconnect::crypto::hmac_sha1(_key, data);
	}
};

inline Hasher* createHasher (aconnect::string_constptr algoName, aconnect::string_constptr param) throw (std::exception) {
	if (NULL == algoName || '\0' == algoName[0])
		return NULL;

	if (util::equals (algoName, consts::HashAlgorithm_Sha1)) {
		return new Sha1Hasher();
	
	} else if (util::equals (algoName, consts::HashAlgorithm_HmacSha1)) {
		
		if (NULL == param || '\0' == param[0])
			throw std::runtime_error ("Invalid key for HMAC-SHA1 algorithm");

		return new HmacSha1Hasher(param);
	}

	return NULL;
}

}}
#endif // ACONNECT_CRYPTO_HASHER_H


