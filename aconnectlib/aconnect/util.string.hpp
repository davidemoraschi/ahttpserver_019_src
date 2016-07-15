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

#ifndef ACONNECT_STRING_UTIL_H
#define ACONNECT_STRING_UTIL_H

#include <boost/algorithm/string.hpp>
#include <boost/scoped_array.hpp>
#include <boost/tokenizer.hpp>
#include <map>

#include "types.hpp"
#include "complex_types.hpp"
#include "error.hpp"

namespace aconnect 
{
	
	typedef boost::tokenizer<boost::char_separator<char> > CharDelimitedTokenizer;

	class SimpleTokenizer : public CharDelimitedTokenizer
	{
		public:
		SimpleTokenizer (string_constref container, string_constptr delimiters = "\r\n") 
			: CharDelimitedTokenizer(container, boost::char_separator<char_type> (delimiters) )
		{
		}

	private:
		SimpleTokenizer& operator = (const SimpleTokenizer&) { return *this; };
	};


	// set of string processing functions
	namespace util 
	{
			
		inline bool isNullOrEmpty (string_constptr str) {	
			return (NULL == str || str[0] == '\0'); 
		}; 

		string decodeUrl (string_constref url);
		
		string encodeUrlPart (string_constref str);
		
		string escapeHtml (string_constref str);
		
		inline string escapeHtml (string_constptr str) 
		{
			if (isNullOrEmpty(str))
				return "";
			return escapeHtml ( string (str) );
		};

		inline char_type parseHexSymbol (char_type symbol) throw (std::out_of_range) 
		{
			switch (symbol) {
			case 'A': case 'a':
				return 10;
			case 'B': case 'b':
				return 11;
			case 'C': case  'c':
				return 12;
			case 'D': case  'd':
				return 13;
			case 'E': case  'e':
				return 14;
			case 'F': case  'f':
				return 15;
			case '0': case '1':	case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': 
				return  ( symbol - 48 );
			default:
				throw std::out_of_range ("Invalid hex symbol");
			}

			return 0;
		}

		inline int compare (string_constptr str1, string_constptr str2, bool ignoreCase = true) {
			if (ignoreCase)
				return stricmp (str1, str2);
			else
				return strcmp (str1, str2);
		};
		
		inline bool equals (string_constptr str1, string_constptr str2, bool ignoreCase = true) {
			return (compare (str1, str2, ignoreCase) == 0);
		};
		inline bool equals (string_constref str1, string_constptr str2, bool ignoreCase = true) {
			return (compare (str1.c_str(), str2, ignoreCase) == 0);
		};
		inline bool equals (string_constref str1, string_constref str2, bool ignoreCase = true) {
			return (compare (str1.c_str(), str2.c_str(), ignoreCase) == 0);
		};
		
		void parseKeyValuePairs (string str, std::map<string, string>& pairs, 
			string_constptr delimiter = ";",
			string_constptr valueTrimSymbols = "\"");

		inline string::size_type findSequence (string_constref input, string_constref seq)
		{
			assert (seq.size() && "Empty sequence to find");

			string::size_type startPos = -1;
			do {
				startPos = input.find (seq[0], startPos + 1);
				if (startPos == string::npos)
					return string::npos;

				string::const_iterator it, seqIt;
				for ( it = input.begin() + startPos + 1, seqIt = seq.begin() + 1
					; it != input.end() && seqIt != seq.end() && *it == *seqIt
					; ++it, ++seqIt);

				if (it == input.end() || seqIt == seq.end()) // found all symbols from sequence beginning
					return startPos;

			} while (startPos != string::npos);
			
			return string::npos;
		}

		inline wide_string getWideString (string_constptr str) throw (std::runtime_error)
		{
			if (isNullOrEmpty(str))
				return wide_string();

			size_t requiredSize = mbstowcs (NULL, str, strlen(str) *  2);
			boost::scoped_array<wchar_t> buff (new wchar_t [requiredSize + 1]);

			size_t size = mbstowcs (buff.get(), str, requiredSize + 1);
			if (size == (size_t) (-1))
				throw std::runtime_error ("Couldn't convert string (MB to wide) - invalid multibyte character.");

			return wide_string (buff.get(), size);
    	}

		inline wide_string getWideString (string_constref str) throw (std::runtime_error)
		{
			return getWideString (str.c_str());
		}

		
		
		inline string getItemFromMap (const str2str_map& dict, string_constref key, string_constptr defaultValue = NULL)
		{
			str2str_map::const_iterator it = dict.find(key);
			if (it != dict.end()) 
				return it->second;
			else if (defaultValue)
				return string (defaultValue);
			
			return string();
		}

		inline bool getItemFromMapBool (const str2str_map& dict, string_constref key, bool defaultValue = false)
		{
			str2str_map::const_iterator it = dict.find(key);
			if (it == dict.end()) 
				return defaultValue;

			if (util::equals (it->second, "true") || util::equals (it->second, "yes") || util::equals (it->second, "1"))
				return true;
			
			return false;
		}

		string getUtf8String (string_constref str) throw (std::runtime_error);
		

#if defined(WIN32)
		
		inline string formatWindowsError (DWORD code)
		{
			char buff[1024] = {0,};
			_snprintf_s (buff, 1024, _TRUNCATE, "Unknown Error 0x%08X", code);
			aconnect::string result = buff;

			char* buffer = NULL;
		
			try
			{
				DWORD res  = ::FormatMessageA
					( FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS
					, NULL
					, code
					, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)
					, (LPSTR) &buffer
					, 0
					, NULL);

				if (res > 0) 
				{
					if (buffer[res - 1] == L'\n')
						buffer[--res] = L'\0';
					if (res > 0 && buffer[res - 1] == L'\r')
						buffer[--res] = L'\0';

					result = buffer;
				}
			}
			catch(...)
			{ }

			if (buffer != NULL)
				::LocalFree(buffer);

			return result;
		}
#endif
	
	}
}


#endif // ACONNECT_STRING_UTIL_H

