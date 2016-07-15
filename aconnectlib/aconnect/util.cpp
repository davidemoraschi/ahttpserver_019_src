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

#include "lib_file_begin.inl"

#if defined (WIN32)
#	include <signal.h>
#elif defined (__GNUC__)
#	include <sys/signal.h>
#	include <dlfcn.h>
#endif  //__GNUC__

#include <boost/scoped_array.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/crc.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>

#include "util.hpp"
#include "util.string.hpp"
#include "util.file.hpp"

#include "complex_types.hpp"


namespace fs = boost::filesystem;
namespace algo = boost::algorithm;

namespace 
{
	inline bool isSafeForUrlPart (aconnect::char_type ch) {
		/*
		if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') )
		return true;
		if (ch == '.' || ch == '-' || ch == '_' || ch == '\'')
		return true;
		return false;
		*/

		static const bool validChars[] = { 
			false, false, false, false, false, false, false, false, false, false,
			false, false, false, false, false, false, false, false, false, false,
			false, false, false, false, false, false, false, false, false, false,
			false, false, false, false, false, false, false, false, false, true,
			false, false, false, false, false, true, true, false, true, true,
			true, true, true, true, true, true, true, true, false, false,
			false, false, false, false, false, true, true, true, true, true,
			true, true, true, true, true, true, true, true, true, true,
			true, true, true, true, true, true, true, true, true, true,
			true, false, false, false, false, true, false, true, true, true,
			true, true, true, true, true, true, true, true, true, true,
			true, true, true, true, true, true, true, true, true, true,
			true, true, true, false, false, false, false, false, false, false,
			false, false, false, false, false, false, false, false, false, false,
			false, false, false, false, false, false, false, false, false, false,
			false, false, false, false, false, false, false, false, false, false,
			false, false, false, false, false, false, false, false, false, false,
			false, false, false, false, false, false, false, false, false, false,
			false, false, false, false, false, false, false, false, false, false,
			false, false, false, false, false, false, false, false, false, false,
			false, false, false, false, false, false, false, false, false, false,
			false, false, false, false, false, false, false, false, false, false,
			false, false, false, false, false, false, false, false, false, false,
			false, false, false, false, false, false, false, false, false, false,
			false, false, false, false, false, false, false, false, false, false,
			false, false, false, false, false };
			
		return validChars [(unsigned char) ch];
	}

	static const aconnect::char_type HexList[] = "0123456789ABCDEF";

	inline void encodeUrlSymbol (aconnect::str_stream& in, aconnect::char_type ch) {
		in << '%' << HexList[(ch >> 4) & 0xF] << HexList[ch & 0xF];
	}
}

namespace aconnect {
	namespace util {
	
	void zeroMemory (void *p, int size)
	{
#ifdef WIN32
		memset(p, 0, (int) size);
#else
		bzero (p, size );
#endif 
	};
	

	//////////////////////////////////////////////////////////////////////////
	//
	//		Filesystem related
	//

	string calculateFileCrc (const std::time_t modifyTime, const size_t fileSize, size_t len)
	{
		boost::crc_32_type  result;
		result.process_bytes ( &modifyTime, sizeof(modifyTime) );
		result.process_bytes ( &fileSize, sizeof(fileSize) );

		string res = boost::lexical_cast <string, boost::crc_32_type::value_type> (result.checksum());
		while (res.length() < len)
			res.insert(0, 1, '0');
		
		return res;
	}

	string calculateFileCrc (aconnect::string_constref filePath, size_t len) 
	{
		std::time_t modifyTime = fs::last_write_time ( filePath);
		std::streamsize fileSize = (std::streamsize) fs::file_size (filePath);

		return calculateFileCrc (modifyTime, fileSize, len);
	}
		

	bool fileExists (string_constref filePath)
	{
		return fs::exists (fs::path (filePath, fs::native));
	}

	string getAppLocation (string_constptr relativePath) throw (std::runtime_error)
	{
		if ( !relativePath || relativePath[0] == '\0' ) 
			throw std::runtime_error ("Empty relative application path");

		fs::path fullPath;
#ifdef WIN32
		const DWORD bufLen = 1024;
		char_type buff[bufLen ];

		DWORD resLen = 0;
		if ( (resLen = ::GetModuleFileNameA( ::GetModuleHandle(NULL), buff, bufLen)) == 0) 
			throw std::runtime_error("Cannot load application startup location");
		
		fullPath.append (buff, buff + resLen);
#else
		fullPath = fs::system_complete (fs::path (relativePath, fs::native) ).normalize();
#endif
		return fullPath.file_string();
	}

	unsigned long getCurrentThreadId()
	{
		unsigned long id = (unsigned long) -1;
#if defined (BOOST_HAS_WINTHREADS)
		id = GetCurrentThreadId();
#elif defined(BOOST_HAS_PTHREADS)
		id = pthread_self();
#elif defined(BOOST_HAS_MPTASKS)
		id = MPCurrentTaskID(); // not tested!
#endif
		return id;
	}
	
	//////////////////////////////////////////////////////////////////////////
	//
	// string translation
	//
	string decodeUrl (string_constref url) 
	{
		if (url.find('%') == string::npos && url.find('+') == string::npos)
			return url;

		string::size_type pos = url.find ('%'), prevPos = 0;
		str_stream res;
		
		do {
			res << algo::replace_all_copy (url.substr (prevPos, pos - prevPos), "+", " ");

			if (pos == string::npos)
				break;

			char_type ch = (parseHexSymbol (url [pos + 1]) << 4 ) + parseHexSymbol (url [pos + 2]);
			res << ch;
			
			prevPos = pos + 3;
			pos = url.find ('%', prevPos);

		} while ( true );
		
		return res.str();
	}

	string encodeUrlPart (string_constref str)
	{
		str_stream encodedUrl;
		string::const_iterator it = str.begin(),
			end = str.end();

		while (it != end) 
		{
			if (isSafeForUrlPart (*it))
				encodedUrl << *it;
			else
				encodeUrlSymbol (encodedUrl, *it);
			++it;
		}
				
		return encodedUrl.str();
	}

	string getUtf8String (string_constref str) throw (std::runtime_error)
	{
		str_stream encoded;
		
		wide_string wstr = getWideString (str);
		wide_string::const_iterator it = wstr.begin(),
			end = wstr.end();

		while (it != end) 
		{
			
			if (*it < 128) {
				encoded << (char_type) *it;
			} else if ((*it > 127) && (*it < 2048)) {

				encoded << (char_type) ((*it >> 6) | 192);
				encoded << (char_type) ((*it & 63) | 128);

			} else {
				encoded << (char_type) ((*it >> 12) | 224);
				encoded << (char_type) (((*it >> 6) & 63) | 128);
				encoded << (char_type) ((*it & 63) | 128);
			}
			++it;
		}

		return encoded.str();
	}

	string escapeHtml (string_constref str) 
	{
		using namespace boost::algorithm;
		// javasript: String (s_).replace (/&/g, "&amp;").replace (/</g, "&lt;").replace (/>/g, "&gt;");	

		string result = replace_all_copy (str, "&", "&amp;");
		replace_all (result, "<", "&lt;" );
		replace_all (result, ">", "&gt;" );

		return result;
	};

	void parseKeyValuePairs (string str, std::map<string, string>& pairs, 
		string_constptr delimiter, string_constptr valueTrimSymbols)
	{
		pairs.clear();
		
		const size_t delimLength = strlen(delimiter);
		string::size_type pos = str.find (delimiter), valuePos;
		do 
		{
			valuePos = str.find ('=');
			if (valuePos < pos) 
				pairs [str.substr (0, valuePos)] = algo::trim_copy_if (str.substr (valuePos + 1, pos - valuePos - 1), 
					algo::is_any_of(valueTrimSymbols) );
			else
				pairs [str.substr (0, pos)] = "";

			str.erase (0, pos + delimLength );
			algo::trim_left (str);

			pos = str.find (delimiter);
			if (pos == string::npos && !str.empty())
				pos = str.size() - 1;

		} while (pos != string::npos);
	}

	//////////////////////////////////////////////////////////////////////////

	void detachFromConsole() throw (std::runtime_error)
	{
		fflush (stdin);
		fflush (stdout);
		fflush (stderr);

#if defined (WIN32)
		/* Detach from the console */
		if ( !FreeConsole() ) 
			throw application_error ("Failed to detach from console (Win32 error = %ld).", GetLastError());
		
#else
		/* Convert to a daemon */
		pid_t pid = fork();
		if (pid < 0)
			throw application_error ("First fork() function failed");
		else if (pid != 0) /* Parent -- exit and leave child running */
			exit (0);

		/* Detach from controlling terminal */
		setsid();

		/* ... and make sure we don't aquire another */
		signal( SIGHUP, SIG_IGN );
		pid = fork();
		if ( pid < 0 )
			throw application_error ("Second fork() function failed");
		
		if ( pid != 0 )
			exit( 0 );
#endif

		/* Close stdio streams because they now have nowhere to go! */
		fclose(stdin);
		fclose(stdout);
		fclose(stderr);
	}

	void unloadLibrary(library_handle_type handle) throw (application_error)
	{
		#if defined (WIN32)
				if (!::FreeLibrary (handle))
					throw application_error ( "DLL unloading failed: %s", formatWindowsError(::GetLastError()).c_str() );
		#else
				if (dlclose (handle))
					throw application_error ( "Library unloading failed: %s", dlerror() );
		#endif
	}

	void* getDllSymbol (library_handle_type handle, string_constptr symbolName, bool mandatory) throw (application_error)
	{
		if (NULL == symbolName || symbolName[0] == '\0')
			throw application_error ("Imvalid symbol name to load from DLL");


		#if defined (WIN32)
			void* res = ::GetProcAddress (handle, symbolName);
			if (mandatory && NULL == res) 
				throw application_error ("DLL symbol loading failed, handle: %d, "
					"symbol: %s, error: %s", 
					handle, 
					symbolName, 
					formatWindowsError(::GetLastError()).c_str());

			
		#else
			void* res = dlsym (handle, symbolName);	
			if (mandatory && NULL == res) 
				throw application_error ("DLL symbol loading failed, "
					"symbol: %s, error: %s", 
					symbolName, 
					dlerror());

		#endif

			return res;
	}

}}
