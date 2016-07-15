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

#ifndef ACONNECT_TYPES_H
#define ACONNECT_TYPES_H


// C4290: C++ exception specification ignored except to indicate a function is not __declspec(nothrow)
#pragma warning( disable : 4290 )
// C4996: 'std::basic_string<_Elem,_Traits,_Ax>::copy': Function call with parameters that may be unsafe - this call relies on the caller to check that the passed values are correct. To disable this warning, use -D_SCL_SECURE_NO_WARNINGS. See documentation on how to use Visual C++ 'Checked Iterators'	d:\work\projects\c++\ahttpserver\ahttpserver\http_request.cpp	93	
#pragma warning( disable : 4996 )

// C4793: 'function' : function is compiled as native code: 'reason'
#pragma warning( disable : 4793 )


// to show which libraries need to be built n boost
#define BOOST_LIB_DIAGNOSTIC


#include <string>
#include <cstdarg>
#ifdef WIN32
#	include <winsock2.h>
#endif

#if defined(__GNUC__)
#	include <cctype>	// for tolower
#
#	if !defined (INVALID_SOCKET)
#		define INVALID_SOCKET (~0)
#	endif
#	if !defined (SOCKET_ERROR)
#		define SOCKET_ERROR (-1)
#	endif
#	if !defined (interface)
#		define interface struct
#	endif
#endif  //__GNUC__

#ifndef NULL
#	define NULL 0
#endif

namespace aconnect
{
	// useful string types
	// now works only with MBCS
	using	std::string;		

	typedef const std::string&				string_constref;
	typedef std::string::pointer			string_ptr;
	typedef std::string::const_pointer		string_constptr;
	
	typedef std::string::value_type			char_type;
	typedef const char_type					string_constant[];

	typedef std::wstring					wide_string;

	// socket's related types
	typedef int err_type;
	typedef int port_type;
	typedef unsigned char byte_type;
	typedef byte_type ip_addr_type[4];

#if defined (__GNUC__)
	typedef int errno_t;
#endif

#ifdef WIN32
	typedef SOCKET socket_type;
	typedef HMODULE library_handle_type;
	typedef HANDLE handle_type;
#elif defined (__GNUC__)
	typedef int socket_type;
	typedef void* library_handle_type;
	typedef void* handle_type;
#endif

	
// some safe functions definition
#if defined(__GNUC__)
#	define vsprintf_s(buff, size, format, arg) vsprintf (buff, format, arg)
#	define mkgmtime timegm
    
    inline int caseInsCharCompareN(char_type a, char_type b) {	return(std::tolower(a) == std::tolower(b)); }
	inline int stricmp (string_constptr str1, string_constptr str2) 
	{
		if (strlen(str1) > strlen(str2))
			return 1;
		if (strlen(str2) > strlen(str1))
			return -1;
			
		for (size_t ndx=0; ndx<strlen(str1); ++ndx)
			if (!caseInsCharCompareN (str1[ndx], str2[ndx]) )
				return (std::tolower(str1[ndx]) > std::tolower(str2[ndx]) ? 1 : -1);
				
		return 0;
	}
	
#elif defined(WIN32)
#	define stricmp  _stricmp
#	define snprintf _snprintf
#	define mkgmtime _mkgmtime
#endif

#define FORMAT_VA_MESSAGE_EX(FormatStr, FormatVarName, OutStr, BuffSize)	\
	va_list args__; \
	va_start (args__, FormatVarName); \
	char buff__[BuffSize]; \
	int cnt__ = vsprintf_s (buff__, BuffSize, FormatStr, args__); \
	aconnect::string OutStr;\
	if (cnt__ > 0)\
	OutStr.append (buff__, cnt__);\
	va_end(args__);

#define FORMAT_VA_MESSAGE(FormatStr, OutStr) FORMAT_VA_MESSAGE_EX(FormatStr, FormatStr, OutStr, 0x1000)

#define FORMAT_VA_MESSAGE_DIRECT(FormatStr, FormatVarName, OutStr) FORMAT_VA_MESSAGE_EX(FormatStr, FormatVarName, OutStr, 0x1000)


#define ARRAY_SIZE(Array) (sizeof(Array)/sizeof(Array[0]))
};

#endif // ACONNECT_TYPES_H
