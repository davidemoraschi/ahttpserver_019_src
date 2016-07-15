/*
This file is part of [ahttp] library. 

Author: Artem Kustikov (kustikoff[at]tut.by)
version: 0.19

This code is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any
damages arising from the use ofreadDirectoryContent this code.

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

// Next strings sometimes can help agains build errors ))
// #if defined(WIN32)
// #define  BOOST_FILESYSTEM_DYN_LINK
// #endif

#include "aconnect/lib_file_begin.inl"

#include <algorithm>
#include <boost/filesystem.hpp>

#include "aconnect/util.time.hpp"
#include "aconnect/util.string.hpp"

#include "ahttp/http_support.hpp"

namespace fs = boost::filesystem;

namespace ahttp 
{ 
	// sample: Sun, 06 Nov 1994 08:49:37 GMT  ; RFC 822, updated by RFC 1123
	string formatDate_RFC1123 (const tm& dateTime) 
	{
		aconnect::char_type buff[32] = {0};

		int cnt = snprintf (buff, 32, "%s, %.2d %s %.4d %.2d:%.2d:%.2d GMT", 
			strings::WeekDays_RFC1123[dateTime.tm_wday],
			dateTime.tm_mday,
			strings::Months_RFC1123[dateTime.tm_mon],

			dateTime.tm_year + 1900,
			dateTime.tm_hour,
			dateTime.tm_min,
			dateTime.tm_sec
			);

		return string (buff, cnt);
	}

	// sample: Sun, 06 Nov 1994 08:49:37 GMT  ; RFC 822, updated by RFC 1123
	std::time_t getDateFrom_RFC1123 (string_constref dt)
	{
		struct tm parsedTm = {0,};
		aconnect::char_type month[5] = {0,};
		aconnect::char_type weekDay[5] = {0,};

		int parsedCnt = sscanf (dt.c_str(), 
			"%3s, %02d %3s %04d %02d:%02d:%02d",
			weekDay,
			&parsedTm.tm_mday,
			month,
			&parsedTm.tm_year,
			&parsedTm.tm_hour,
			&parsedTm.tm_min,
			&parsedTm.tm_sec);

		assert (parsedCnt == 7 && "Cannot parse RFC1123 date");

		parsedTm.tm_year -= 1900;

		for (int ndx=0; ndx<12; ++ndx) {
			if (strcmp(strings::Months_RFC1123[ndx], month) == 0) {
				parsedTm.tm_mon = ndx;
				break;
			}
		}

		std::time_t res = mkgmtime (&parsedTm); // return UTC date-time

		return res;
	}

	bool sortWdByTypeAndName (const WebDirectoryItem& item1, const WebDirectoryItem& item2)
	{
		if (item1.type != item2.type)
			return item1.type < item2.type;
		return item1.name < item2.name;
	}

	void readDirectoryContent (string_constref dirPath,
							   string_constref dirVirtPath,
							   std::vector<WebDirectoryItem> &items, 
							   aconnect::Logger& logger,
							   size_t &errCount,
							   aconnect::IStopable *stopable,
							   WebDirectorySortType sortType)
	{

		aconnect::ProgressTimer progress (logger, __FUNCTION__);

		fs::directory_iterator endTter;
		WebDirectoryItem item;
				
		for ( fs::directory_iterator dirIter (dirPath);
			dirIter != endTter;
			++dirIter )
		{
			try
			{
				if (stopable && stopable->isStopped())
					return;
				
				item.name = dirIter->path().leaf();
				item.lastWriteTime = fs::last_write_time (dirIter->path());

				if ( fs::is_directory( dirIter->status() ) ) {
					item.type = WdDirectory;
					item.url = dirVirtPath + aconnect::util::encodeUrlPart (item.name) + strings::Slash;

				} else  {
					item.type = WdFile;
					item.url = dirVirtPath + aconnect::util::encodeUrlPart (item.name);
					item.size = fs::file_size (dirIter->path());
				}

				items.push_back (item);
				item.clear();

			} catch (fs::basic_filesystem_error<fs::path> &err) {
				logger.error ("Directory content loading failed - 'basic_filesystem_error' caught: %s, "
					"system error code: %d, path 1: %s, path 2: %s", 
					err.what(), err.system_error(),
					err.path1().string().c_str(),
					err.path2().string().c_str());
				++errCount;
			
			} catch (fs::filesystem_error &err) {
				logger.error ("Directory info loading failed - 'filesystem_error' caught: %s, "
					"system error code: %d", 
					err.what(), err.system_error());
				++errCount;


			} catch ( const std::exception& ex ) {
				logger.error ("Exception caught at directory \"%s\" content loading (%s): %s", 
					dirPath.c_str(),
					typeid(ex).name(), ex.what());
				++errCount;
			}

			if (sortType == WdSortByTypeAndName)
				std::sort(items.begin(), items.end(), sortWdByTypeAndName);
		}

	}
}

