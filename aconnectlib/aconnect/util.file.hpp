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

#ifndef ACONNECT_FILE_UTIL_H
#define ACONNECT_FILE_UTIL_H

#include <boost/algorithm/string.hpp>


#include "types.hpp"
#include "error.hpp"

namespace aconnect 
{
	/** 
	 * set of utility functions working with filesystem
	 */
	namespace util 
	{
		string calculateFileCrc (const std::time_t modificationTime, const size_t fileSize, size_t len = 16);
		string calculateFileCrc (aconnect::string_constref filePath, size_t len = 16);

		string getAppLocation (string_constptr relativePath) throw (std::runtime_error);
		bool fileExists (string_constref filePath);
	
	}
}


#endif // ACONNECT_FILE_UTIL_H

