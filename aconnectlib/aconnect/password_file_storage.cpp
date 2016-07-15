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
#include <boost/filesystem.hpp>
#include <fstream>

#include "util.string.hpp"
#include "crypto/password_file_storage.hpp"

namespace aconnect { namespace crypto {
	
	namespace fs = boost::filesystem;

	PasswordFileStorage::PasswordFileStorage () : 
		_checkFileCrc (true)
	{
		
	}

	void PasswordFileStorage::init (string_constref filePath, bool checkFileCrc) throw (application_error) {
		
		_filePath = filePath;
		_checkFileCrc = checkFileCrc;

		if (!fs::exists(_filePath)) {
			_filePath.clear();
			throw application_error ("Passwords file not found: %s", filePath.c_str());
		}

		processFile();	
	}

	
	void PasswordFileStorage::processFile() {
		assert (fs::exists(_filePath) && "Passwords file not found");
		
		_passwords.clear();

		if (_checkFileCrc)
			_fileCrc = util::calculateFileCrc (_filePath);
	
		std::ifstream file (_filePath.c_str(), std::ios::binary);
		
		const int lineSize = 128;
		char_type line[lineSize] = {0,};


		while (file.getline(line, lineSize)) {
			char_type *pos = strstr( line, ":" );
			if (pos != NULL) {
				int nameLen = pos - line;
				
				++pos; // skip ':'
				while (*pos == ' ' || *pos == '\t')
					++pos;

				int passLen = strlen(pos);
				while (pos[passLen - 1] == '\r' || pos[passLen - 1] == '\n') {
					pos[passLen - 1] = '\0';
					--passLen;
				}

				
				string pass (pos, passLen);
				_passwords[string(line, nameLen)] = pass;
			}
		}
	
		file.close();
	}

	PasswordCheckResult PasswordFileStorage::passwordValid (string_constref userName, string_constref password) {
				
		string storedPassword;

		if (_checkFileCrc) 
		{
			boost::mutex::scoped_lock lock (_fileLoadMutex);
			string src = util::calculateFileCrc (_filePath);
			
			if (!util::equals (src, _fileCrc, false))
				processFile ();

			if ( !loadPassword (userName, storedPassword) )
				return PasswordCheckUserNotFound;
		
		} else {
			if ( !loadPassword (userName, storedPassword) )
				return PasswordCheckUserNotFound;
		}
		
		return (storedPassword == password ? PasswordCheckValid  : PasswordCheckInvalid);
	}

	bool PasswordFileStorage::loadPassword (string_constref userName, string& storedPassword) {
		str2str_map::const_iterator passIter;

		passIter = _passwords.find(userName);
		if (passIter == _passwords.end())
			return false;

		storedPassword = passIter->second;
		return true;
	}

	




}};
