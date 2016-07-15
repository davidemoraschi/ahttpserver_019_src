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

/*
	Idea got from SHA-1 (Secure Hashing Algorithm) implementation
	by Paul E. Jones.

	----------------------------------------------------------------
	Copyright (C) 1998
	Paul E. Jones <paulej@arid.us>
	All Rights Reserved.

	This software is licensed as "freeware."  Permission to distribute
	this software in source and binary forms is hereby granted without
	a fee.  THIS SOFTWARE IS PROVIDED 'AS IS' AND WITHOUT ANY EXPRESSED
	OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
	WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
	THE AUTHOR SHALL NOT BE HELD LIABLE FOR ANY DAMAGES RESULTING
	FROM THE USE OF THIS SOFTWARE, EITHER DIRECTLY OR INDIRECTLY, INCLUDING,
	BUT NOT LIMITED TO, LOSS OF DATA OR DATA BEING RENDERED INACCURATE.
	----------------------------------------------------------------
*/

#ifndef ACONNECT_CRYPTO_SHA1_H
#define ACONNECT_CRYPTO_SHA1_H

#include <stdexcept>

#include "aconnect/types.hpp"

namespace aconnect { namespace crypto {
	
	typedef unsigned int variable_type; // sizeof(variable_type) >= sizeof(WORD)

	const int Sha1DigestLength = 5; // words
	const char_type HexSymbolsList[] = "0123456789abcdef";
	

	struct sha1
	{
		variable_type	digest[Sha1DigestLength];			// Message Digest (output)
		char_type		digestBytes[Sha1DigestLength * 4];	// Message Digest in bytes 

		variable_type lengthLow;			// Message length in bits (low word)
		variable_type lengthHigh;			// Message length in bits (hi word)

		byte_type messageBlock[64];			// 512-bit message blocks - type for byte must be unsigned
		int messageBlockIndex;				// Index into message block array

		bool computed;						// Is the digest computed?
		bool corrupted;						// Is the message digest corruped?

		inline sha1 ();
		inline sha1 (string_constref input);

		///////////////////////////////////////////////////////////////////////////////
		//
		//		Methods
		
		/**
		*	Initialize the sha1 object in preparation for computing a new message digest.
		*/
		inline void reset ();

		inline void append (string_constref input);
		inline void append (string_constptr input, string::size_type inputSize);

		/**
		*	According to the standard, the message must be padded to an even
		*   512 bits.  The first padding bit must be a '1'.  The last 64
		*   bits represent the length of the original message.  All bits in
		*   between should be 0.  This function will pad the message
		*   according to those rules by filling the 'messageBlock' array
		*   accordingly.  It will also call processMessageBlock()
		*   appropriately.  When it returns, it can be assumed that the
		*   message digest has been computed. 
		*/
		inline void calculate ();

		/**
		*	Process the current 512 bits of the message stored in the messageBlock array
		*/
		inline void processMessageBlock();

		inline bool isValid ();

		/**
		* Return calculated SHA-1 hash
		* @param[in]	formatHex - Flag shows whether format output in hex
		*/
		inline string result (bool formatHex = true) throw (std::runtime_error);

		inline void processDigest (string_constptr input, string::size_type inputSize, char_type *outDigest) throw (std::runtime_error);

		inline void validate()  throw (std::runtime_error);


		/**
		*	Perform following sequence: reset/append/result
		*	@param[in]	input - Data to calculate digest
		*	@param[in]	formatHex - Flag shows whether format output in hex
		*/
		inline string process (string_constref input, bool formatHex = true) throw (std::runtime_error);

		// helpers
		inline static variable_type leftRotate (int bits, variable_type in);
	};

	sha1::sha1() 
	{
		reset();
	}

	sha1::sha1 (string_constref input) {
		reset();
		append (input);
	}

	void sha1::reset () {
		lengthLow = lengthHigh = 0;
		messageBlockIndex = 0;

		digest[0]      = 0x67452301;
		digest[1]      = 0xEFCDAB89;
		digest[2]      = 0x98BADCFE;
		digest[3]      = 0x10325476;
		digest[4]      = 0xC3D2E1F0;

		computed = false;
		corrupted  = false;
	}
	
	bool sha1::isValid () {
		return !corrupted;
	}

	void sha1::append (string_constref input) {
		append (input.c_str(), input.size());
	}
	
	void sha1::append (string_constptr input, string::size_type inputSize) 
	{
		if (!inputSize)
			return;
		
		// calculated hash cannot be modified
		if (computed || corrupted) {
			corrupted = true;
			return;
		}

		while(inputSize-- && !corrupted)
		{
			messageBlock[messageBlockIndex++] = (byte_type) *input;

			lengthLow += 8;
			
			// Force it to 32 bits
			lengthLow &= 0xFFFFFFFF;

			if (lengthLow == 0)
			{
				++lengthHigh;

				// Force it to 32 bits
				lengthHigh &= 0xFFFFFFFF;
				if (lengthHigh == 0)
				{
					// Message is too long
					corrupted = true;
					return;
				}
			}

			if (messageBlockIndex == 64)
				processMessageBlock();
			
			++input;
		}
	}

	void sha1::validate() throw (std::runtime_error) {
		if (corrupted)
			throw std::runtime_error("SHA-1 hash calculation corrupted");
	}

	string sha1::result(bool formatHex)  throw (std::runtime_error) 
	{
		validate ();

		if (!computed)
			calculate ();
		
		if (formatHex) 
		{
			string res ("0000000000000000000000000000000000000000");
			
			for (int ndx=0; ndx<Sha1DigestLength * 4; ++ndx) {
				res[ndx*2] = HexSymbolsList[(digestBytes[ndx] >> 4) & 0xF];
				res[ndx*2 + 1] = HexSymbolsList[digestBytes[ndx] & 0xF];
			}
			return res;
		}

		return string (digestBytes, Sha1DigestLength * 4);
	}

	void sha1::processDigest (string_constptr input, string::size_type inputSize, char_type *outDigest) throw (std::runtime_error)
	{
		reset ();
		append (input, inputSize);

		validate ();
		calculate ();

		std::copy (&digestBytes[0], &digestBytes[0] + Sha1DigestLength * 4, outDigest);
	}

	string sha1::process (string_constref input, bool formatHex) throw (std::runtime_error)
	{
		reset ();
		append (input);

		return result(formatHex);
	}
	
	variable_type sha1::leftRotate (int bits, variable_type word) 
	{
		return ((word << bits) & 0xFFFFFFFF) | ((word & 0xFFFFFFFF) >> (32-bits));
 	}

	void sha1::calculate ()
	{
		//	Check to see if the current message block is too small to hold
		//  the initial padding bits and length.  If so, we will pad the
		//  block, process it, and then continue padding into a second
		//  block.
		
		if (messageBlockIndex > 55) // 64 - 8 - 1
		{
			messageBlock[messageBlockIndex++] = (char_type) 0x80;
			
			while(messageBlockIndex < 64)
				messageBlock[messageBlockIndex++] = 0;
			
			processMessageBlock();

			while(messageBlockIndex < 56)
				messageBlock[messageBlockIndex++] = 0;
			
		}
		else
		{
			messageBlock[messageBlockIndex++] = (char_type) 0x80;
			while(messageBlockIndex < 56)
				messageBlock[messageBlockIndex++] = 0;
			
		}

		// Store the message length as the last 8 octets
		
		messageBlock[56] = (lengthHigh >> 24) & 0xFF;
		messageBlock[57] = (lengthHigh >> 16) & 0xFF;
		messageBlock[58] = (lengthHigh >> 8) & 0xFF;
		messageBlock[59] = (lengthHigh) & 0xFF;
		messageBlock[60] = (lengthLow >> 24) & 0xFF;
		messageBlock[61] = (lengthLow >> 16) & 0xFF;
		messageBlock[62] = (lengthLow >> 8) & 0xFF;
		messageBlock[63] = (lengthLow) & 0xFF;

		processMessageBlock();

		digestBytes[3]  = (char_type) ( digest[0]       & 0xff);
		digestBytes[2]  = (char_type) ((digest[0] >> 8) & 0xff);
		digestBytes[1]  = (char_type) ((digest[0] >> 16) & 0xff);
		digestBytes[0]  = (char_type) ((digest[0] >> 24) & 0xff);

		digestBytes[7]  = (char_type) ( digest[1]       & 0xff);
		digestBytes[6]  = (char_type) ((digest[1] >> 8) & 0xff);
		digestBytes[5]  = (char_type) ((digest[1] >> 16) & 0xff);
		digestBytes[4]  = (char_type) ((digest[1] >> 24) & 0xff);

		digestBytes[11]  = (char_type) ( digest[2]       & 0xff);
		digestBytes[10]  = (char_type) ((digest[2] >> 8) & 0xff);
		digestBytes[9] = (char_type) ((digest[2] >> 16) & 0xff);
		digestBytes[8] = (char_type) ((digest[2] >> 24) & 0xff);

		digestBytes[15] = (char_type) ( digest[3]       & 0xff);
		digestBytes[14] = (char_type) ((digest[3] >> 8) & 0xff);
		digestBytes[13] = (char_type) ((digest[3] >> 16) & 0xff);
		digestBytes[12] = (char_type) ((digest[3] >> 24) & 0xff);

		digestBytes[19] = (char_type) ( digest[4]       & 0xff);
		digestBytes[18] = (char_type) ((digest[4] >> 8) & 0xff);
		digestBytes[17] = (char_type) ((digest[4] >> 16) & 0xff);
		digestBytes[16] = (char_type) ((digest[4] >> 24) & 0xff);

		computed = true;
	}

	void sha1::processMessageBlock () 
	{
		const unsigned K[] =            // Constants defined in SHA-1
		{
			0x5A827999,
			0x6ED9EBA1,
			0x8F1BBCDC,
			0xCA62C1D6
		};
		int				t;					// Loop counter
		variable_type   temp;				// Temporary word value
		variable_type	W[80] = {0};		// Word sequence
		variable_type   A, B, C, D, E;		// Word buffers

		// Initialize the first 16 words in the array W
		for(t = 0; t < 16; ++t)
		{
			W[t] = ((variable_type) messageBlock[t * 4]) << 24;
			W[t] |= ((variable_type) messageBlock[t * 4 + 1]) << 16;
			W[t] |= ((variable_type) messageBlock[t * 4 + 2]) << 8;
			W[t] |= ((variable_type) messageBlock[t * 4 + 3]);
		}

		for(t = 16; t < 80; ++t)
		{
			W[t] = leftRotate(1, W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16]);
		}

		A = digest[0];
		B = digest[1];
		C = digest[2];
		D = digest[3];
		E = digest[4];

		for(t = 0; t < 20; ++t)
		{
			temp =  leftRotate(5,A) + ((B & C) | ((~B) & D)) + E + W[t] + K[0];
			temp &= 0xFFFFFFFF;
			E = D;
			D = C;
			C = leftRotate(30,B);
			B = A;
			A = temp;
		}

		for(t = 20; t < 40; ++t)
		{
			temp = leftRotate(5,A) + (B ^ C ^ D) + E + W[t] + K[1];
			temp &= 0xFFFFFFFF;
			E = D;
			D = C;
			C = leftRotate(30,B);
			B = A;
			A = temp;
		}

		for(t = 40; t < 60; ++t)
		{
			temp = leftRotate(5,A) + ((B & C) | (B & D) | (C & D)) + E + W[t] + K[2];
			temp &= 0xFFFFFFFF;
			E = D;
			D = C;
			C = leftRotate(30,B);
			B = A;
			A = temp;
		}

		for(t = 60; t < 80; ++t)
		{
			temp = leftRotate(5,A) + (B ^ C ^ D) + E + W[t] + K[3];
			temp &= 0xFFFFFFFF;
			E = D;
			D = C;
			C = leftRotate(30,B);
			B = A;
			A = temp;
		}

		digest[0] = (digest[0] + A) & 0xFFFFFFFF;
		digest[1] = (digest[1] + B) & 0xFFFFFFFF;
		digest[2] = (digest[2] + C) & 0xFFFFFFFF;
		digest[3] = (digest[3] + D) & 0xFFFFFFFF;
		digest[4] = (digest[4] + E) & 0xFFFFFFFF;

		messageBlockIndex = 0;
	}

	

}} // namespace aconnect::crypto 

#endif // ACONNECT_CRYPTO_SHA1_H


