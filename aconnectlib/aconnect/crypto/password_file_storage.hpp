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

#ifndef ACONNECT_CRYPTO_PASSWORD_FILE_STORAGE_H
#define ACONNECT_CRYPTO_PASSWORD_FILE_STORAGE_H

#include <boost/thread.hpp>

#include "../complex_types.hpp"
#include "../util.file.hpp"
#include "../error.hpp"


namespace aconnect { namespace crypto {
	
	enum PasswordCheckResult
	{
		PasswordCheckValid,
		PasswordCheckInvalid,
		PasswordCheckUserNotFound
	};

	class PasswordFileStorage
	{
	public:
			PasswordFileStorage ();
			
			PasswordCheckResult passwordValid (string_constref userName, string_constref password);
			
			void init (string_constref filePath, bool checkFileCrc = true) throw (application_error);

	protected:
			void processFile();
			bool loadPassword (string_constref userName, string& storedPassword);

	protected:
		bool _checkFileCrc;
		string _filePath;
		str2str_map _passwords;
		string _fileCrc;

		boost::mutex _fileLoadMutex;
	};

}};


#endif // ACONNECT_CRYPTO_PASSWORD_FILE_STORAGE_H

