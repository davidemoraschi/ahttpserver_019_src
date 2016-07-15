/*
	Artem Kustikov
	Code got from
	http://www.nuclex.org/downloads/developers/snippets/base64-encoder-and-decoder-in-cxx

	Modifications:
	- types updated to aconnect types
	- added global namespace aconnect::thirdparty
	- removed Hungarian notation in variables names ))) 

*/

// -----------------------------------------------------------------------//
//  ####   ###     ##                -= Base64 library =-                 //
//  #   # #       # # Base64.h - Base64 encoder/decoder                   //
//  ####  ####   #  #                                                     //
//  #   # #   # ##### Encodes and decodes base64 strings                  //
//  #   # #   #     # Ideas taken from work done by Bob Withers           //
//  ####   ###      # R1                      2002-05-07 by Markus Ewald  //
// -----------------------------------------------------------------------//

#ifndef B64_BASE64_H
#define B64_BASE64_H

#include "../types.hpp"

namespace aconnect { namespace thirdparty {

namespace Base64 
{

  /// Encode string to base64
  inline string encode(string_constref input);
  /// Encode base64 into string
  inline string decode(string_constref input);

  const char_type FillChar = '=';

}; // namespace Base64

// ####################################################################### //
// # Base64::encode()                                                    # //
// ####################################################################### //
/** Encodes the specified string to base64

    @param  input  String to encode
    @return Base64 encoded string
*/
inline string Base64::encode(string_constref input) 
{
	static const string Base64Table(
		// 0000000000111111111122222222223333333333444444444455555555556666
		// 0123456789012345678901234567890123456789012345678901234567890123
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
		);

	string::size_type   length = input.length();
	string              result;

	// Allocate memory for the converted string
	result.reserve(length * 8 / 6 + 1);

	for(string::size_type pos = 0; pos < length; pos++) 
	{
		char code;

		// Encode the first 6 bits
		code = (input[pos] >> 2) & 0x3f;
		result.append(1, Base64Table[code]);

		// Encode the remaining 2 bits with the next 4 bits (if present)
		code = (input[pos] << 4) & 0x3f;
		if(++pos < length)
			code |= (input[pos] >> 4) & 0x0f;
		result.append(1, Base64Table[code]);

		if(pos < length) {
			code = (input[pos] << 2) & 0x3f;
			if(++pos < length)
				code |= (input[pos] >> 6) & 0x03;

			result.append(1, Base64Table[code]);

		} else {
			++pos;
			result.append(1, FillChar);
		}

		if(pos < length) {
			code = input[pos] & 0x3f;
			result.append(1, Base64Table[code]);

		} else {
			result.append(1, FillChar);
		}
	}

  return result;
}

// ####################################################################### //
// # Base64::decode()                                                    # //
// ####################################################################### //
/** Decodes the specified base64 string

    @param  input  Base64 string to decode
    @return Decoded string
*/
inline string Base64::decode(string_constref input) 
{
	static const string::size_type np = string::npos;
	static const string::size_type DecodeTable[] = 
	{
		// 0   1   2   3   4   5   6   7   8   9 
		np, np, np, np, np, np, np, np, np, np,  //   0 -   9
		np, np, np, np, np, np, np, np, np, np,  //  10 -  19
		np, np, np, np, np, np, np, np, np, np,  //  20 -  29
		np, np, np, np, np, np, np, np, np, np,  //  30 -  39
		np, np, np, 62, np, np, np, 63, 52, 53,  //  40 -  49
		54, 55, 56, 57, 58, 59, 60, 61, np, np,  //  50 -  59
		np, np, np, np, np,  0,  1,  2,  3,  4,  //  60 -  69
		5,  6,  7,  8,  9, 10, 11, 12, 13, 14,  //  70 -  79
		15, 16, 17, 18, 19, 20, 21, 22, 23, 24,  //  80 -  89
		25, np, np, np, np, np, np, 26, 27, 28,  //  90 -  99
		29, 30, 31, 32, 33, 34, 35, 36, 37, 38,  // 100 - 109
		39, 40, 41, 42, 43, 44, 45, 46, 47, 48,  // 110 - 119
		49, 50, 51, np, np, np, np, np, np, np,  // 120 - 129
		np, np, np, np, np, np, np, np, np, np,  // 130 - 139
		np, np, np, np, np, np, np, np, np, np,  // 140 - 149
		np, np, np, np, np, np, np, np, np, np,  // 150 - 159
		np, np, np, np, np, np, np, np, np, np,  // 160 - 169
		np, np, np, np, np, np, np, np, np, np,  // 170 - 179
		np, np, np, np, np, np, np, np, np, np,  // 180 - 189
		np, np, np, np, np, np, np, np, np, np,  // 190 - 199
		np, np, np, np, np, np, np, np, np, np,  // 200 - 209
		np, np, np, np, np, np, np, np, np, np,  // 210 - 219
		np, np, np, np, np, np, np, np, np, np,  // 220 - 229
		np, np, np, np, np, np, np, np, np, np,  // 230 - 239
		np, np, np, np, np, np, np, np, np, np,  // 240 - 249
		np, np, np, np, np, np                   // 250 - 256
	};

	string::size_type length = input.length();
	string            result;

	result.reserve(length);

	for(string::size_type pos = 0; pos < length; pos++) 
	{
		unsigned char c, c1;

		c = (char) DecodeTable[(unsigned char)input[pos]];
		pos++;
		c1 = (char) DecodeTable[(unsigned char)input[pos]];
		c = (c << 2) | ((c1 >> 4) & 0x3);
		result.append(1, c);

		if(++pos < length) {
			c = input[pos];
			if(FillChar == c)
				break;

			c = (char) DecodeTable[(unsigned char)input[pos]];
			c1 = ((c1 << 4) & 0xf0) | ((c >> 2) & 0xf);
			result.append(1, c1);
		}

		if(++pos < length) {
			c1 = input[pos];
			if(FillChar == c1)
				break;

			c1 = (char) DecodeTable[(unsigned char)input[pos]];
			c = ((c << 6) & 0xc0) | c1;
			result.append(1, c);
		}
	}

	return result;
}

}} // namespace aconnect::thirdparty

#endif // B64_BASE64_H
