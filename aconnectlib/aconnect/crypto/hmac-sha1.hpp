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

/******************************************************/
/* hmac-sha1()                                        */
/* Performs the hmac-sha1 keyed secure hash algorithm */
/******************************************************/


#ifndef ACONNECT_CRYPTO_HMAC_SHA1_H
#define ACONNECT_CRYPTO_HMAC_SHA1_H


namespace aconnect { namespace crypto {

	const int MaxHmacSha1MEssageLength = 4096;

	inline string hmac_sha1(string_constptr key, string::size_type keyLength,
					 string_constptr data, string::size_type dataLength,
					 bool formatHex = true)

	{
		
		const string::size_type blockSize = 64; // block size
		byte_type ipad = 0x36;
		byte_type opad = 0x5c;

		byte_type k0[64] = {0,};
		byte_type k0xorIpad[64] = {0,};
		byte_type step7data[64] = {0,};
		
		char_type step5data[MaxHmacSha1MEssageLength + 128] = {0,};
		char_type step8data[blockSize + 20] = {0,};
		
		char_type digest[Sha1DigestLength * 4]  = {0,}; // store output

		string::size_type i;
		
		sha1 sha1Hasher;
		
		/*
		for (i=0; i<blockSize; ++i)
		{
			k0[i] = 0x00;
		}
		*/


		if (keyLength != blockSize)    // Step 1
		{
			// Step 2
			if (keyLength > blockSize)      
			{
				sha1Hasher.processDigest (key, keyLength, digest);

				for (i=0; i < 20; ++i)
					k0[i] = (byte_type) digest[i];
				
			// Step 3
			} else if (keyLength < blockSize)   {
				
				for (i=0; i < keyLength; ++i)
					k0[i] = key[i];
			}
		}
		else
		{
			for (i=0; i < blockSize; ++i)
				k0[i] = key[i];
		}
		// Step 4
		for (i = 0; i < blockSize; i++)
			k0xorIpad[i] = k0[i] ^ ipad;
		
		// Step 5
		for (i=0; i < blockSize; i++)
			step5data[i] = k0xorIpad[i];
		
		for (i=0; i < dataLength; i++)
			step5data[i+blockSize] = data[i];
		
		// Step 6
		sha1Hasher.processDigest (step5data, dataLength + blockSize, digest);

		// Step 7
		for (i=0; i<blockSize; i++)
			step7data[i] = k0[i] ^ opad;
		
		// Step 8
		for (i=0; i < blockSize; i++)
			step8data[i] = step7data[i];
		
		for (i=0; i<20; i++)
			step8data[ i+ blockSize] = digest[i];
		
		// Step 9
		sha1Hasher.processDigest (step8data, blockSize + 20, digest);


		if (formatHex) 
		{
			string res ("0000000000000000000000000000000000000000");
			
			for (int ndx=0; ndx<Sha1DigestLength * 4; ++ndx) {
				res[ndx*2] = HexSymbolsList[(digest[ndx] >> 4) & 0xF];
				res[ndx*2 + 1] = HexSymbolsList[digest[ndx] & 0xF];
			}
			return res;
		}

		return string (digest, Sha1DigestLength * 4);

	}

	inline string hmac_sha1(string_constref key, string_constref data, bool formatHex = true)
	{
		return hmac_sha1 (key.c_str(), key.size(), data.c_str(), data.size(), formatHex);
	}


}} // namespace aconnect::crypto 

#endif // ACONNECT_CRYPTO_HMAC_SHA1_H


