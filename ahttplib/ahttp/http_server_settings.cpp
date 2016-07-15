/*
This file is part of [ahttp] library. 

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

#include "aconnect/lib_file_begin.inl"

#include "aconnect/boost_format_safe.hpp"

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include <assert.h>

#include "tinyxml/tinyxml.h"

#include "aconnect/util.hpp"
#include "aconnect/util.string.hpp"

#include "ahttp/http_support.hpp"
#include "ahttp/http_server_settings.hpp"

#if defined(__GNUC__)
#	include <dlfcn.h>
#endif

namespace algo = boost::algorithm;
namespace fs = boost::filesystem;

#include "http_messages.inl"

namespace ahttp
{
	class PluginInfoByNameFinder
	{
	public:
		PluginInfoByNameFinder (string_constref name) : _name (name)	{}
		
		bool operator () (const PluginRegistrationInfo& info) {
			return (aconnect::util::equals (info.pluginName, _name));
		}
		
	protected:
		string _name;
	};

	DirectorySettings::DirectorySettings () : 
		number (0),
		browsingEnabled (Tristate::Undefined), 
		isLinkedDirectory(false),
		maxRequestSize (-1),
		enableParentPathAccess (Tristate::Undefined)
	{

	}

	HttpServerSettings::HttpServerSettings() :
		InstanceId (1),
		_port(-1), 
		_commandPort (-1),
		_logLevel (aconnect::Log::Debug), 				   
		_maxLogFileSize (aconnect::Log::MaxFileSize), 
		_enableKeepAlive (defaults::EnableKeepAlive),
		_keepAliveTimeout (defaults::KeepAliveTimeout),
		_commandSocketTimeout (defaults::CommandSocketTimeout),
		_responseBufferSize (defaults::ResponseBufferSize),
		_maxChunkSize (defaults::MaxChunkSize),
		_logger (NULL),
		_serverVersion (defaults::ServerVersion),
		_firstLoad (true),
		_loaded (false),
		_directoryConfigFile (defaults::DirectoryConfigFile),
		_messagesFile (defaults::DirectoryConfigFile),
		_uploadCreationTriesCount (defaults::UploadCreationTriesCount)
	{
		_settings.socketReadTimeout = defaults::ServerSocketTimeout;
		_settings.socketWriteTimeout = defaults::ServerSocketTimeout;

		initMessages ();
	}

	HttpServerSettings::~HttpServerSettings()
	{
	}

	string HttpServerSettings::getMimeType (string_constref ext) const 
	{
		using namespace aconnect;
		
		str2str_map::const_iterator it = _mimeTypes.find(ext);
		if (it != _mimeTypes.end())
			return it->second;

		return strings::ContentTypeOctetStream;			
	}

	const DirectorySettings* HttpServerSettings::getDirSettingsByName(string_constref dirName) const	
	{
		directories_map::const_iterator it = _directories.begin();

		for (;it != _directories.end(); ++it) {
			if (aconnect::util::equals(it->second.name, dirName)) 
				return &it->second;
		}
		
		return NULL;			
	}


	void HttpServerSettings::load (string_constptr docPath) throw (settings_load_error)
	{
		_loaded = false;

		if ( !docPath || docPath[0] == '\0' ) 
			throw settings_load_error ("Empty settings file path to load");

		TiXmlDocument doc( docPath );
		bool loadOkay = doc.LoadFile();

		if ( !loadOkay ) {
			boost::format msg ("Could not load settings file \"%s\". Error=\"%s\".");
			msg % docPath;
			msg % doc.ErrorDesc();

			throw settings_load_error (boost::str (msg).c_str());
		}
		
		TiXmlElement* root = doc.RootElement( );
		assert ( root );
		if ( !aconnect::util::equals(root->Value(), SettingsTags::RootElement, false)  ) 
			throw settings_load_error ("Invalid setting root element");
		
		if (_firstLoad)
		{
			TiXmlElement* serverElem = root->FirstChildElement (SettingsTags::ServerElement);
			assert (serverElem);
			if ( !serverElem ) 
				throw settings_load_error ("Cannot find <server> element");

			// server setup
			loadServerSettings (serverElem);

			TiXmlElement* logElement = serverElem->FirstChildElement (SettingsTags::LogElement);
			if ( !serverElem ) 
				throw settings_load_error ("Cannot find <log> element");

			// logger setup
			loadLoggerSettings (logElement);
		
		} 
		else 
		{
			// clear directories info
			_directories.clear();
		}
		
		TiXmlElement* directoryElem = root->FirstChildElement (SettingsTags::DirectoryElement);
		if ( !directoryElem ) 
			throw settings_load_error ("At least one <directory> element must be");

		std::vector <DirectorySettings> directoriesList;
		do {
			directoriesList.push_back ( loadDirectory (directoryElem));
			directoryElem = directoryElem->NextSiblingElement(SettingsTags::DirectoryElement);
		
		} while (directoryElem);

		std::vector <DirectorySettings>::iterator it = directoriesList.end();
		for (it = directoriesList.begin(); 
			it != directoriesList.end() && it->name != _rootDirName; ++it );

		if (it == directoriesList.end())
			throw settings_load_error ("There is no <directory> record with name \"%s\"", 
				_rootDirName.c_str());

		if (it->realPath.empty())
			throw settings_load_error ("Empty path in root <directory> record");
		
		// work with filesystem
		try
		{
			fs::path rootPath (it->realPath, fs::portable_directory_name);
			if ( !fs::exists (rootPath) )
				throw settings_load_error ("Incorrect path in root <directory> record - path does not exist");
			if ( !fs::is_directory (rootPath) )
				throw settings_load_error ("Incorrect path in root <directory> record - target is not a directory");

			it->realPath = rootPath.directory_string();
			it->virtualPath = strings::Slash;

			// try to load local directory configuration
			fs::path dirConfigFile = fs::path (it->realPath, fs::native) / fs::path(_directoryConfigFile, fs::portable_file_name);
			tryLoadLocalSettings (dirConfigFile.string(), *it);

			// setup defaults
			if (it->maxRequestSize == (size_t) -1) // is not loaded
				it->maxRequestSize = defaults::MaxRequestSize;

			// register root
			it->number = 0;
			loadGlobalModules (*it);

			_directories [it->virtualPath] = *it;
	
			fillDirectoriesMap (directoriesList, it);
			
			fillModulesCallbackInfo();

			_firstLoad = false;
			_loaded = true;

		} catch (fs::basic_filesystem_error<fs::path> &err) {
			throw settings_load_error (
				"Directories info loading failed - 'basic_filesystem_error' caught: %s, "
				"system error code: %d, path 1: %s, path 2: %s", 
				err.what(), err.system_error(),
				err.path1().string().c_str(),
				err.path2().string().c_str()); 

		} catch (fs::filesystem_error &err) {
			throw settings_load_error (
				"Directories info loading failed - 'filesystem_error' caught: %s, "
				"system error code: %d", 
				err.what(), err.system_error());

		} catch (std::exception &ex)  {
			throw settings_load_error ("Directories info loading failed - exception [%s]: %s", 
				typeid(ex).name(), ex.what());;

		} catch (...)  {
			throw settings_load_error ("Directories info loading failed - unknown exception caught");
		}
	}

	void HttpServerSettings::fillDirectoriesMap (std::vector<DirectorySettings>& directoriesList, 
											 std::vector<DirectorySettings>::iterator parent)
	{
		using namespace aconnect;
		
		
		std::vector <DirectorySettings>::iterator childIter;
		for (childIter = directoriesList.begin(); childIter != directoriesList.end(); ++childIter ) 
		{
			if (childIter->parentName == parent->name) 
			{
				if (childIter->virtualPath.empty())
					throw settings_load_error ("Empty <virtual-path> for nested directory: %s", 
						childIter->name.c_str());
				string virtualPathInit = childIter->virtualPath;

				childIter->virtualPath = parent->virtualPath + childIter->virtualPath;
				if (childIter->virtualPath.substr(childIter->virtualPath.length() - 1) != strings::Slash)
					childIter->virtualPath += strings::Slash;

				// get real path
				fs::path childPath;
				if (!childIter->realPath.empty()) {
					childPath = fs::path (childIter->realPath, fs::portable_directory_name);
					childIter->isLinkedDirectory = true;
					childIter->relativePath = virtualPathInit;
				} else {
					childPath = fs::complete (fs::path (childIter->relativePath, fs::portable_directory_name),
										fs::path (parent->realPath, fs::native));
				}
				
				if ( !fs::exists (childPath) )
					throw settings_load_error ("Incorrect path in <directory> record - path does not exist,\
											   directory: %s", childIter->name.c_str());
				if ( !fs::is_directory (childPath) )
					throw settings_load_error ("Incorrect path in <directory> record - target is not a directory,\
											   directory: %s", childIter->name.c_str());

				childIter->realPath = childPath.directory_string();
				
				// try to load local directory configuration
				fs::path dirConfigFile = fs::path (childIter->realPath, fs::native) / fs::path(_directoryConfigFile, fs::portable_file_name);

				tryLoadLocalSettings (dirConfigFile.string(), *childIter);

				// copy properties
				if (childIter->browsingEnabled == Tristate::Undefined) childIter->browsingEnabled = parent->browsingEnabled;
				if (childIter->charset.empty()) childIter->charset = parent->charset;
				if (childIter->maxRequestSize == (size_t) -1) childIter->maxRequestSize = parent->maxRequestSize;
				if (childIter->enableParentPathAccess == Tristate::Undefined) childIter->enableParentPathAccess = parent->enableParentPathAccess;

				if (childIter->headerTemplate.empty())	childIter->headerTemplate = parent->headerTemplate;
				if (childIter->parentDirectoryTemplate.empty())	childIter->parentDirectoryTemplate = parent->parentDirectoryTemplate;
				if (childIter->fileTemplate.empty())	childIter->fileTemplate = parent->fileTemplate;
				if (childIter->directoryTemplate.empty())	childIter->directoryTemplate = parent->directoryTemplate;
				if (childIter->virtualDirectoryTemplate.empty())	childIter->virtualDirectoryTemplate = parent->virtualDirectoryTemplate;
				if (childIter->footerTemplate.empty())	childIter->footerTemplate = parent->footerTemplate;

				// fill default documents list
				default_documents_vector defDocsList = parent->defaultDocuments;

				default_documents_vector::iterator iter = childIter->defaultDocuments.begin(), removeIter;
				while (iter != childIter->defaultDocuments.end()) 
				{
					if (iter->first && std::find (defDocsList.begin(), defDocsList.end(), *iter) == defDocsList.end()) {
						defDocsList.push_back(*iter);
					
					} else if (!iter->first) {
						removeIter = std::find (defDocsList.begin(), defDocsList.end(), std::make_pair(true, iter->second));
						if (removeIter == defDocsList.end())
							throw settings_load_error ("Cannot remove default document record \"%s\", in directory: %s - "
								"it is not declared in parent directory record.", 
									iter->second.c_str(), 
									childIter->name.c_str());
						else
							defDocsList.erase(removeIter);
					}
					iter++;
				}
				childIter->defaultDocuments = defDocsList;
				
				// copy plugins registration
				copyHandlersRegistration (*parent, *childIter);

				copyModulesRegistration (*parent, *childIter);
				
				// store updated directory info
				childIter->number = _directories.size();
				_directories[childIter->virtualPath] = *childIter;
				
				fillDirectoriesMap (directoriesList, childIter);
			}

		}

	}

	
	void HttpServerSettings::loadGlobalModules (DirectorySettings& root)
	{
		// load global modules
		global_plugins_map::const_iterator moduleIt = _registeredModules.begin();
		
		for (; moduleIt != _registeredModules.end(); ++moduleIt) 
		{
			if (moduleIt->second.global) 
			{
				const PluginInfo& info = moduleIt->second;
				
				PluginRegistrationInfo regInfo;

				regInfo.registrationType = PluginRegistration::Add;
				regInfo.pluginName = moduleIt->first;
				regInfo.pluginIndex = info.pluginIndex;

				std::copy (&info.moduleCallbacks[0], 
					&info.moduleCallbacks[0] + ModuleCallbackTypesCount,
					&regInfo.moduleCallbacks[0]);
		
				root.modules.push_back ( regInfo );
			}
		}
	

	}

	void HttpServerSettings::addModuleCallbacksToMap (void** moduleCallbacks, int pluginIndex, callback_map& callbackMap )
	{
		for (int ndx=0; ndx<ModuleCallbackTypesCount; ++ndx)
		{
			if (moduleCallbacks[ndx])
			{
				callback_map::iterator it = callbackMap.find ((ModuleCallbackType) ndx);
				if (it == callbackMap.end()) {
					std::list< std::pair <void*, int> > lst;
					lst.push_back ( std::make_pair (moduleCallbacks[ndx], pluginIndex));

					callbackMap.insert ( std::make_pair ((ModuleCallbackType) ndx, lst)); 
				
				} else {
					it->second.push_back ( std::make_pair (moduleCallbacks[ndx], pluginIndex) );
				}
			}
		
		}
	}


	void HttpServerSettings::fillModulesCallbackInfo ()
	{
		_modulesCallbacks.clear();
	
		directories_map::const_iterator dirIter = _directories.begin();
	
		for (; dirIter != _directories.end(); ++dirIter) 
		{
			callback_map dirCallbacks;

			directory_plugins_list::const_iterator it = dirIter->second.modules.begin();
			for (; it != dirIter->second.modules.end(); ++it) 
			{
				addModuleCallbacksToMap ( (void**) it->moduleCallbacks, it->pluginIndex, dirCallbacks);
			}
			
			_modulesCallbacks[dirIter->second.number] = dirCallbacks;
		}
	}


	void HttpServerSettings::copyHandlersRegistration (const DirectorySettings& parent, DirectorySettings& child)
	{
		directory_plugins_list registeredHandlers = parent.handlers;
		directory_plugins_list::const_iterator handlerIter;

		for (handlerIter = child.handlers.begin(); 
				handlerIter != child.handlers.end(); ++handlerIter) 
		{
			if (handlerIter->registrationType == PluginRegistration::Clear) {
				registeredHandlers.clear();
			
			} else if (handlerIter->registrationType == PluginRegistration::Remove) {
				
				// if no 'ext' attribute defined - remove registration at all
				if (handlerIter->extensions.empty()) {
					registeredHandlers.erase (std::remove_if (registeredHandlers.begin(), 
								registeredHandlers.end(), PluginInfoByNameFinder(handlerIter->pluginName) ), 
							registeredHandlers.end());
				} else {
					directory_plugins_list::iterator it = std::find_if (registeredHandlers.begin(), 
						registeredHandlers.end(), PluginInfoByNameFinder(handlerIter->pluginName) );

					if (it != registeredHandlers.end()) 
					{
					    std::set<string>::const_iterator itExt;
					    
					    for (itExt = handlerIter->extensions.begin(); 
					            itExt != handlerIter->extensions.end(); ++itExt)  {
					        if ( it->extensions.find (*itExt) != it->extensions.end())
					            it->extensions.erase (*itExt);
					    }
				
						it->excludedExtensions.insert (handlerIter->extensions.begin(), handlerIter->extensions.end());
					}
				}
			
			} else if (handlerIter->registrationType == PluginRegistration::Add) {
				
				directory_plugins_list::iterator it = std::find_if (registeredHandlers.begin(), 
						registeredHandlers.end(), PluginInfoByNameFinder(handlerIter->pluginName) );

				if (it == registeredHandlers.end())
					registeredHandlers.push_back(*handlerIter);
				else
					it->extensions.insert (handlerIter->extensions.begin(), handlerIter->extensions.end());
			
			} else if (handlerIter->registrationType == PluginRegistration::Register) {
				// not safe, but will give some freedom od action for user	
				registeredHandlers.push_back(*handlerIter);
			} 
		}

		// apply handlers info intersection
		child.handlers.swap(registeredHandlers);
	}

	void HttpServerSettings::copyModulesRegistration (const DirectorySettings& parent, DirectorySettings& child)
	{
		directory_plugins_list registeredModules = parent.modules;
		
		directory_plugins_list::const_iterator iter;

		for (iter = child.modules.begin(); 
				iter != child.modules.end(); ++iter) 
		{
			if (iter->registrationType == PluginRegistration::Clear) {
				registeredModules.clear();
			
			} else if (iter->registrationType == PluginRegistration::Remove) {
				
				registeredModules.erase (std::remove_if (registeredModules.begin(), 
					registeredModules.end(), PluginInfoByNameFinder(iter->pluginName) ), 
							registeredModules.end());
			
			} else if (iter->registrationType == PluginRegistration::Add
				|| iter->registrationType == PluginRegistration::Register) {
		
				registeredModules.push_back(*iter);
			} 
		}

		// apply modules info intersection
		child.modules.swap(registeredModules);
	}

	void HttpServerSettings::tryLoadLocalSettings (string_constref filePath, DirectorySettings& dirInfo) 
		throw (settings_load_error)
	{

		if (fs::exists (filePath))
		{
			TiXmlDocument doc( filePath.c_str() );

			if ( !doc.LoadFile() ) {
				boost::format msg ("Could not load local directory config file \"%s\". Error=\"%s\".");
				msg % filePath;
				msg % doc.ErrorDesc();

				throw settings_load_error (boost::str (msg).c_str());
			}

			TiXmlElement* root = doc.RootElement( );

			if ( !aconnect::util::equals(root->Value(), SettingsTags::DirectoryElement, false)  ) 
				throw settings_load_error ("Invalid local directory config file root element, file: %s",
					filePath.c_str());

			loadLocalDirectorySettings (root, dirInfo);
		}
	}

	void HttpServerSettings::loadServerSettings (TiXmlElement* serverElem) throw (settings_load_error)
	{
		using namespace aconnect;

		string_constptr strValue;
		string stringValue;

		// version
		strValue = serverElem->Attribute (SettingsTags::VersionAttr);
		if (!util::isNullOrEmpty(strValue))
			_serverVersion = strValue;

		// port
		if (!loadIntAttribute (serverElem, SettingsTags::PortAttr, _port))
			throw settings_load_error ("Port number loading failed");
		
		// command port
		if (!loadIntAttribute (serverElem, SettingsTags::CommandPortAttr, _commandPort))
			throw settings_load_error ("Command port number loading failed");

		// IP address - OPTIONAL
		if (loadStringAttribute (serverElem, SettingsTags::IpAddressAttr, stringValue))
			if (! util::parseIpAddr(stringValue, _settings.ip)) 
				throw settings_load_error ("Invalid IP address in server config");
			
		// workers count - OPTIONAL
		loadIntAttribute (serverElem, SettingsTags::WorkersCountAttr, _settings.workerLifeTime);

		// pooling - OPTIONAL
		loadBoolAttribute (serverElem, SettingsTags::PoolingEnabledAttr, _settings.enablePooling);

		// worker life time - OPTIONAL
		loadIntAttribute (serverElem, SettingsTags::WorkerLifeTimeAttr, _settings.workerLifeTime);
		
		// read timeouts
		loadIntAttribute(serverElem, SettingsTags::ServerSocketTimeoutAttr, _settings.socketWriteTimeout);
		loadIntAttribute(serverElem, SettingsTags::ServerSocketTimeoutAttr, _settings.socketReadTimeout);


		// load keep-alive mode
		loadBoolAttribute (serverElem, SettingsTags::KeepAliveEnabledAttr, _enableKeepAlive);
		loadIntAttribute(serverElem, SettingsTags::KeepAliveTimeoutAttr, _keepAliveTimeout);

		loadIntAttribute(serverElem, SettingsTags::CommandSocketTimeoutAttr, _commandSocketTimeout);

		// directory configuration file
		loadStringAttribute (serverElem, SettingsTags::DirectoryConfigFileAttr, 
			_directoryConfigFile, true);

		// messages file
		loadStringAttribute (serverElem, SettingsTags::MessagesFileAttr, _messagesFile, true);

		// uploads directory setup
		loadStringAttribute (serverElem, SettingsTags::UploadsDirAttr, _globalUploadsDirectory, true);
		loadIntAttribute (serverElem, SettingsTags::UploadCreationTriesCountAttr, _uploadCreationTriesCount);

		// load locale an apply it,
		// locale should be defined to perform correct MBSTR->WIDE conversion
		
		if (loadStringAttribute (serverElem, SettingsTags::LocaleAttr, stringValue) &&
		    !stringValue.empty()) 
		{
			char* res = setlocale (LC_CTYPE, stringValue.c_str());
            
            if (NULL == res)
				throw settings_load_error ("Unsupported locale defined: %s", stringValue.c_str());
		}
				
		if (!_globalUploadsDirectory.empty()) 
		{
			updateAppLocationInPath (_globalUploadsDirectory);
			fs::path uploadsDir (_globalUploadsDirectory, fs::native); 

			if (!fs::exists (uploadsDir)) {
				try {
					fs::create_directories(uploadsDir);
				
				} catch (std::runtime_error& err) {
					throw settings_load_error("Upload directory creation failed, exception: %s, message: %s", 
						typeid(err).name(), err.what());
				}
			}
		}
		
		// read HTTP settings
		strValue = serverElem->Attribute (SettingsTags::ResponseBufferSizeAttr);
		if (!util::isNullOrEmpty(strValue))
			_responseBufferSize = boost::lexical_cast<size_t> (strValue);

		strValue = serverElem->Attribute (SettingsTags::MaxChunkSizeAttr);
		if (!util::isNullOrEmpty(strValue))
			_maxChunkSize = boost::lexical_cast<size_t> (strValue);

		// root directory
		if ( !loadStringAttribute (serverElem, SettingsTags::RootAttr, _rootDirName) ) 
			throw settings_load_error ("Invalid root directory name");
		
		TiXmlElement* mimeTypesElement = serverElem->FirstChildElement (SettingsTags::MimeTypesElement);
		if (!mimeTypesElement)
			throw settings_load_error ("<%s> not found in server settings", SettingsTags::MimeTypesElement);

		loadMimeTypes (mimeTypesElement);

		// read server's handlers registration info
		TiXmlElement* handlersElem = serverElem->FirstChildElement (SettingsTags::HandlersElement);
		if (handlersElem)
			loadPlugins (handlersElem, PluginHandler);
		
		// read server's modules registration info
		TiXmlElement* modulesElem = serverElem->FirstChildElement (SettingsTags::ModulesElement);
		if (modulesElem)
			loadPlugins (modulesElem, PluginModule);

		loadMessages ();
	}

	void HttpServerSettings::loadLoggerSettings (TiXmlElement* logElement) throw (settings_load_error)
	{
		using namespace aconnect;
		assert (logElement);
		string_constptr strValue;

		// load level
		strValue = logElement->Attribute( SettingsTags::LogLevelAttr );
		if (stricmp (strValue, Log::CriticalMsg) == 0)
			_logLevel = Log::Critical;
		else if (stricmp (strValue, Log::ErrorMsg) == 0)
			_logLevel = Log::Error;
		else if (stricmp (strValue, Log::WarningMsg) == 0)
			_logLevel = Log::Warning;
		else if (stricmp (strValue, Log::InfoMsg) == 0)
			_logLevel = Log::Info;
		else 
			_logLevel = Log::Debug;
		
		int intValue = 0, getAttrRes;
		
		// load max file size
		getAttrRes = logElement->QueryIntAttribute (SettingsTags::MaxFileSizeAttr, &intValue );
		if (getAttrRes == TIXML_SUCCESS)
			_maxLogFileSize = intValue;

		TiXmlElement* pathElement = logElement->FirstChildElement (SettingsTags::PathElement);
		if ( NULL == pathElement ) 
			throw settings_load_error ("Log file path loading failed: <%s> is mandatory element", 
				SettingsTags::PathElement);

		strValue = pathElement->GetText();
		if ( util::isNullOrEmpty(strValue) ) 
			throw settings_load_error ("Invalid log file template");
		
		_logFileTemplate = strValue;
	}


	DirectorySettings HttpServerSettings::loadDirectory (TiXmlElement* directoryElem) throw (settings_load_error)
	{
		using namespace aconnect;
		assert (directoryElem);
		
		DirectorySettings ds;
		string strValue;
		string_constptr strPtrValue;
		int getAttrRes = 0, 
			intAttr = 0;

		// load name
		getAttrRes = directoryElem->QueryValueAttribute( SettingsTags::NameAttr, &ds.name);
		if (getAttrRes != TIXML_SUCCESS)
			throw settings_load_error ("Directory does not have \"%s\" attribute",
				SettingsTags::NameAttr);
		
		// load browsing-enabled
		loadTristateAttribute (directoryElem, SettingsTags::BrowsingEnabledAttr, ds.browsingEnabled);
					
		// load charset
		loadStringAttribute (directoryElem, SettingsTags::CharsetAttr, ds.charset);
		
		// load max-request-size
		if (loadIntAttribute (directoryElem, SettingsTags::MaxRequestSizeAttr, intAttr))
			ds.maxRequestSize = intAttr;
		
		// load browsing-enabled
		loadTristateAttribute (directoryElem, SettingsTags::EnableParentPathAccess, ds.enableParentPathAccess);
		
		// load parent
		getAttrRes = directoryElem->QueryValueAttribute( SettingsTags::ParentAttr, &strValue);
		if (getAttrRes == TIXML_SUCCESS) 
			ds.parentName = strValue;

		// path
		bool realPathDefined = false;
		TiXmlElement* pathElement = directoryElem->FirstChildElement (SettingsTags::PathElement);
		if (pathElement) {
			strPtrValue = pathElement->GetText();
			realPathDefined = true;

			if ( util::isNullOrEmpty(strPtrValue) ) 
				throw settings_load_error ("Empty path attribute for directory: %s", ds.name.c_str());
			ds.realPath = strPtrValue;
			
			updateAppLocationInPath (ds.realPath);
		}

		// virtual-path
		pathElement = directoryElem->FirstChildElement (SettingsTags::VirtualPathElement);
		if (pathElement) {
			strPtrValue = pathElement->GetText();
			if ( !util::isNullOrEmpty(strPtrValue) )
				ds.virtualPath = strPtrValue;
		}

		// relative-path
		pathElement = directoryElem->FirstChildElement (SettingsTags::RelativePathElement);
		if (pathElement) {
			if (realPathDefined)
				throw settings_load_error ("<%s> and <%s> must not be defined together,\
						directory: %s",
						SettingsTags::PathElement,
						SettingsTags::RelativePathElement,
						ds.name.c_str());
			
			strPtrValue = pathElement->GetText();
			if ( !util::isNullOrEmpty(strPtrValue) ) 
				ds.relativePath = strPtrValue;
		}


		TiXmlElement* fileTemplate = directoryElem->FirstChildElement (SettingsTags::FileTemplateElement);
		TiXmlElement* directoryTemplate = directoryElem->FirstChildElement (SettingsTags::DirectoryTemplateElement);
		TiXmlElement* virtualDirectoryTemplate = directoryElem->FirstChildElement (SettingsTags::VirtualDirectoryTemplateElement);
		TiXmlElement* parentDirectoryTemplate = directoryElem->FirstChildElement (SettingsTags::ParentDirectoryTemplateElement);

		if ( (!fileTemplate && directoryTemplate) || (fileTemplate && !directoryTemplate))
			throw settings_load_error ("<directory-template> and <file-template> should be defined together, directory: %s", ds.name.c_str());

		if (ds.browsingEnabled == Tristate::True && !fileTemplate)
			throw settings_load_error ("<directory-template> and <file-template> must be defined together,\
									   when browsing enabled, directory: %s", ds.name.c_str());

		if (fileTemplate && directoryTemplate) {
			strPtrValue = fileTemplate->GetText();		if (strPtrValue) ds.fileTemplate = strPtrValue;
			strPtrValue = directoryTemplate->GetText();	if (strPtrValue) ds.directoryTemplate = strPtrValue;
		}
		if (virtualDirectoryTemplate && (strPtrValue = virtualDirectoryTemplate->GetText()) ) 
			ds.virtualDirectoryTemplate = strPtrValue;
		
		if (parentDirectoryTemplate && (strPtrValue = parentDirectoryTemplate->GetText())) 
			ds.parentDirectoryTemplate = strPtrValue;
		
		TiXmlElement* headerTemplate = directoryElem->FirstChildElement (SettingsTags::HeaderTemplateElement);
		if (headerTemplate && (strPtrValue = headerTemplate->GetText()))
				ds.headerTemplate = strPtrValue;
		
		TiXmlElement* footerTemplate = directoryElem->FirstChildElement (SettingsTags::FooterTemplateElement);
		if (footerTemplate && (strPtrValue = footerTemplate->GetText()) )
			ds.footerTemplate = strPtrValue;
		

		// add tabulators and EOLs
		algo::replace_all (ds.headerTemplate, SettingsTags::TabulatorMark, "\t");
		algo::replace_all (ds.parentDirectoryTemplate, SettingsTags::TabulatorMark, "\t");
		algo::replace_all (ds.fileTemplate, SettingsTags::TabulatorMark, "\t");
		algo::replace_all (ds.directoryTemplate, SettingsTags::TabulatorMark, "\t");
		algo::replace_all (ds.virtualDirectoryTemplate, SettingsTags::TabulatorMark, "\t");
		algo::replace_all (ds.footerTemplate, SettingsTags::TabulatorMark, "\t");

		algo::replace_all (ds.headerTemplate, SettingsTags::EndOfLineMark, "\r\n");
		algo::replace_all (ds.parentDirectoryTemplate, SettingsTags::EndOfLineMark, "\r\n");
		algo::replace_all (ds.fileTemplate, SettingsTags::EndOfLineMark, "\r\n");
		algo::replace_all (ds.directoryTemplate, SettingsTags::EndOfLineMark, "\r\n");
		algo::replace_all (ds.virtualDirectoryTemplate, SettingsTags::EndOfLineMark, "\r\n");
		algo::replace_all (ds.footerTemplate, SettingsTags::EndOfLineMark, "\r\n");

		
		loadLocalDirectorySettings (directoryElem, ds);

		return ds;
	}

	void HttpServerSettings::loadLocalDirectorySettings (class TiXmlElement* directoryElem, DirectorySettings& dirInfo) 
		throw (settings_load_error) 
	{
		// load default documents
		TiXmlElement* defDocumentsElem = directoryElem->FirstChildElement (SettingsTags::DefaultDocumentsElement);
		if (defDocumentsElem)
			loadDirectoryIndexDocuments (defDocumentsElem, dirInfo);
		
		// load directory handlers
		TiXmlElement* handlersElem = directoryElem->FirstChildElement (SettingsTags::HandlersElement);
		if (handlersElem)
			loadDirectoryPlugins (handlersElem, dirInfo, PluginHandler);

		// load directory modules
		TiXmlElement* modulesElem = directoryElem->FirstChildElement (SettingsTags::ModulesElement);
		if (modulesElem)
			loadDirectoryPlugins (modulesElem, dirInfo, PluginModule);

		// load directory mappings
		TiXmlElement* mappings = directoryElem->FirstChildElement (SettingsTags::MappingsElement);
		if (mappings)
			loadDirectoryMappings (mappings, dirInfo);

	}


	void HttpServerSettings::updateAppLocationInPath (string &pathStr) const 
	{
		if ( pathStr.find(SettingsTags::AppPathMark) != string::npos)
				algo::replace_first (pathStr, SettingsTags::AppPathMark, _appLocaton);
	}

	void HttpServerSettings::loadMimeTypes (class TiXmlElement* mimeTypesElement) throw (settings_load_error)
	{
		string_constptr filePath =
			mimeTypesElement->Attribute(SettingsTags::FileAttr);

		if ( filePath )
		{
			string filePathStr (filePath);
			
			updateAppLocationInPath (filePathStr);
		
			TiXmlDocument doc;
			bool loaded = doc.LoadFile (filePathStr);

			if ( !loaded ) {
				boost::format msg ("Could not load MIME-types definition file \"%s\". Error=\"%s\".");
				msg % filePathStr;
				msg % doc.ErrorDesc();
				throw settings_load_error (boost::str (msg).c_str());
			}

			TiXmlElement* root = doc.RootElement( );
			assert ( root );
			if ( !aconnect::util::equals (root->Value(), SettingsTags::MimeTypesElement, false) ) 
				throw settings_load_error ("Invalid root element in MIME-types definition file");

			loadMimeTypes (root);
		}

		// load sub-nodes

		TiXmlElement* typeElem = mimeTypesElement->FirstChildElement (SettingsTags::TypeElement);
		if ( !typeElem ) 
			return;
		
		string_constptr strPtrValue;
		string strValue;
		int getAttrRes = 0;

		do {
			// load name
			getAttrRes = typeElem->QueryValueAttribute( SettingsTags::ExtAttr, &strValue);
			if (getAttrRes == TIXML_NO_ATTRIBUTE)
				throw settings_load_error ("<%s> does not have \"%s\" attribute",
					SettingsTags::TypeElement, 
					SettingsTags::ExtAttr);

			strPtrValue = typeElem->GetText(); 
			if (strPtrValue) 
				_mimeTypes [strValue] = strPtrValue;

			typeElem = typeElem->NextSiblingElement (SettingsTags::TypeElement);

		} while (typeElem);
	}

	void HttpServerSettings::loadPlugins (class TiXmlElement* pluginsElement, PluginType pluginType) 
		throw (settings_load_error)
	{
		using namespace aconnect;

		TiXmlElement* pluginElem = pluginsElement->FirstChildElement (SettingsTags::RegisterElement);
		
		// make continuous numbering
		int index = (int) (_registeredHandlers.size() + _registeredModules.size());

		while (pluginElem) 
		{
			PluginInfo info;
			info.pluginIndex = index++;

			string pluginName = loadPluginRegistration (pluginElem, info, pluginType);
			
			loadPluginProperties (pluginName, info, pluginType);

			pluginElem = pluginElem->NextSiblingElement (SettingsTags::RegisterElement);
		}
	}


	string HttpServerSettings::loadPluginRegistration (class TiXmlElement* pluginElem, 
		PluginInfo& info, 
		PluginType pluginType)
			throw (settings_load_error)
	{
		string pluginName;

		if (!loadStringAttribute(pluginElem, SettingsTags::NameAttr, pluginName, true))
			throw settings_load_error ("Global <%s> does not have \"%s\" attribute - it is required",
				SettingsTags::RegisterElement, 
				SettingsTags::NameAttr);

		if (pluginType == PluginHandler) {

			if (loadStringAttribute(pluginElem, SettingsTags::DefaultExtAttr, info.defaultExtension)) {
				if (info.defaultExtension.empty())
					throw settings_load_error ("Handler \"%s\" has empty \"%s\" attribute",
						pluginName.c_str(), 
						SettingsTags::DefaultExtAttr);
			}
		
		} else if (pluginType == PluginModule) {
			loadBoolAttribute(pluginElem, SettingsTags::GlobalAttr, info.global);
		}
		
		TiXmlElement* childElem = pluginElem->FirstChildElement(SettingsTags::PathElement);
		
		if (!childElem)
			throw settings_load_error ("Plugin \"%s\" has no <%s> element ", 
				pluginName.c_str(), 
				SettingsTags::PathElement);

		string_constptr path = childElem->GetText();

		if (aconnect::util::isNullOrEmpty(path))
			throw settings_load_error ("Plugin \"%s\" has empty <%s> element ", 
				pluginName.c_str(), 
				SettingsTags::PathElement);

		info.pathToLoad = path;

		// load parameters
		childElem = pluginElem->FirstChildElement (SettingsTags::ParameterElement);
		
		string paramName;
		
		while (childElem) 
		{
			if ( !loadStringAttribute(childElem, SettingsTags::NameAttr, paramName, true) )
				throw settings_load_error ("<%s> for handler \"%s\" have no \"%s\" attribute",
					SettingsTags::ParameterElement, 
					pluginName.c_str(), 
					SettingsTags::NameAttr );

			string_constptr value = childElem->GetText();
			if (NULL != value)
				info.params[paramName] = value;

			childElem = childElem->NextSiblingElement (SettingsTags::ParameterElement);
		}

		return pluginName;
	}


	
	void HttpServerSettings::loadPluginProperties (string_constref pluginName, 
		PluginInfo& info, 
		PluginType pluginType) throw (settings_load_error)
	{
		updateAppLocationInPath (info.pathToLoad);

		global_plugins_map& pluginsMap = (pluginType == PluginHandler ? _registeredHandlers : _registeredModules);
		if (pluginsMap.find (pluginName) != pluginsMap.end() )
			throw settings_load_error ("Plugin \"%s\" has been already loaded", pluginName.c_str());

#if defined (WIN32)		
		info.dll = ::LoadLibraryA (info.pathToLoad.c_str());

		if (NULL == info.dll)
			throw settings_load_error ("Plugin loading failed, library: %s", info.pathToLoad.c_str());

		if (pluginType == PluginHandler)
		{
			info.processRequestFunc = ::GetProcAddress (info.dll, SettingsTags::ProcessRequestFunName);
			if (!info.processRequestFunc) 
				throw settings_load_error ("Request processing function loading failed, "
					"library: %s, error code: %d", info.pathToLoad.c_str(), ::GetLastError());
		}
		
		info.initFunc = ::GetProcAddress (info.dll, SettingsTags::InitFunName);
		if (!info.initFunc) 
			throw settings_load_error ("Plugin initialization function loading failed, "
				"library: %s, error code: %d", info.pathToLoad.c_str(), ::GetLastError());

		info.destroyFunc = ::GetProcAddress (info.dll, SettingsTags::DestroyFunName);

#else
		info.dll = dlopen (info.pathToLoad.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND );
		if (NULL == info.dll) {
			string_constptr errorMsg = dlerror (); 
			throw settings_load_error ("Plugin loading failed, library: %s, error: %s", 
				info.pathToLoad.c_str(), errorMsg ? errorMsg : "Unknown error");
		}

		if (pluginType == PluginHandler)
		{
			info.processRequestFunc = dlsym (info.dll, SettingsTags::ProcessRequestFunName);
			if (!info.processRequestFunc) 
				throw settings_load_error ("Request processing function loading failed, "
					"library: %s, error: %s", info.pathToLoad.c_str(), dlerror());
		}

		info.initFunc = dlsym (info.dll, SettingsTags::InitFunName);
		if (!info.initFunc) 
			throw settings_load_error ("Plugin initialization function loading failed, "
				"library: %s, error: %s", info.pathToLoad.c_str(), dlerror());

		info.destroyFunc = dlsym (info.dll, SettingsTags::DestroyFunName);
#endif
		if (pluginType == PluginModule)
		{
			info.moduleCallbacks[ModuleCallbackOnRequestBegin] = aconnect::util::getDllSymbol (info.dll, SettingsTags::OnRequestBeginFunName);
			info.moduleCallbacks[ModuleCallbackOnRequestResolve] = aconnect::util::getDllSymbol (info.dll, SettingsTags::OnRequestResolveFunName);
			info.moduleCallbacks[ModuleCallbackOnRequestMapHandler] = aconnect::util::getDllSymbol (info.dll, SettingsTags::OnRequestMapHandlerName);
			
			info.moduleCallbacks[ModuleCallbackOnResponsePreSendHeaders] = aconnect::util::getDllSymbol (info.dll, SettingsTags::OnResponsePreSendHeadersFunName);
			info.moduleCallbacks[ModuleCallbackOnResponsePreSendContent] = aconnect::util::getDllSymbol (info.dll, SettingsTags::OnResponsePreSendContentFunName);
			info.moduleCallbacks[ModuleCallbackOnResponseEnd] = aconnect::util::getDllSymbol (info.dll, SettingsTags::OnResponseEndFunName);
		}

		if (pluginType == PluginHandler)
			_registeredHandlers[pluginName] = info;
		else if (pluginType == PluginModule)
			_registeredModules[pluginName] = info;
	}

	void HttpServerSettings::loadDirectoryIndexDocuments (class TiXmlElement* documentsElement, DirectorySettings& ds) 
		throw (settings_load_error)
	{
		string_constptr strPtrValue;

		TiXmlElement* elem = documentsElement->FirstChildElement (SettingsTags::AddElement);
		while (elem) {
			if ( (strPtrValue = elem->GetText()) ) 
				ds.defaultDocuments.push_back (std::make_pair (true, strPtrValue));

			elem = elem->NextSiblingElement (SettingsTags::AddElement);
		}
		
		elem = documentsElement->FirstChildElement(SettingsTags::RemoveElement);
		while (elem) {
			if ( (strPtrValue = elem->GetText()) ) 
				ds.defaultDocuments.push_back (std::make_pair (false, strPtrValue));
			elem = elem->NextSiblingElement (SettingsTags::RemoveElement);
		}
	}

	
	void HttpServerSettings::loadDirectoryPlugins (class TiXmlElement* pluginsElement, 
		DirectorySettings& dirInfo,
		PluginType pluginType) 
		throw (settings_load_error)
	{
		using namespace aconnect;
		
		string ext, name;
		std::set<string> extList;
		int getAttrRes;

		TiXmlElement* item = pluginsElement->FirstChildElement ();

		global_plugins_map& pluginsMap = (pluginType == PluginHandler ? _registeredHandlers : _registeredModules);
		directory_plugins_list& targetPluginsList = (pluginType == PluginHandler ? dirInfo.handlers : dirInfo.modules);
		
		// process add, remove, clear, register elements
		while (item) 
		{
			PluginRegistrationInfo regInfo;

			if (util::equals(item->Value(), SettingsTags::RegisterElement) )
			{
				PluginInfo info;
				info.pluginIndex = (int) (_registeredHandlers.size() + _registeredModules.size());

				// load plugin into server
				string pluginName = loadPluginRegistration (item, info, pluginType);
				loadPluginProperties (pluginName, info, pluginType);

				regInfo.pluginName = pluginName;
				regInfo.registrationType = PluginRegistration::Register;
				regInfo.pluginIndex = info.pluginIndex;

				if (pluginType == PluginHandler) {
					regInfo.processFunc = info.processRequestFunc;
				} else {
					std::copy (&info.moduleCallbacks[0], 
							&info.moduleCallbacks[0] + ModuleCallbackTypesCount,
							&regInfo.moduleCallbacks[0]);
				}
											
				targetPluginsList.push_back ( regInfo );
			}
			else if (util::equals(item->Value(), SettingsTags::AddElement) )
			{
				getAttrRes = item->QueryValueAttribute( SettingsTags::NameAttr, &name);
				if (getAttrRes == TIXML_NO_ATTRIBUTE)
					throw settings_load_error ("<%s> does not have \"%s\" attribute, directory: %s",
						SettingsTags::RegisterElement, SettingsTags::NameAttr, dirInfo.name.c_str());
				
				global_plugins_map::iterator foundPlugin = pluginsMap.find(name);
				if (foundPlugin == pluginsMap.end())
					throw settings_load_error ("Plugins\"%s\" is not registered, directory: %s",
						name.c_str(), dirInfo.name.c_str());
				
				const PluginInfo& info = foundPlugin->second;

				if (pluginType == PluginHandler)
				{
					getAttrRes = item->QueryValueAttribute( SettingsTags::ExtAttr, &ext);
					if (info.defaultExtension.empty() && (getAttrRes == TIXML_NO_ATTRIBUTE || ext.empty() ))
						throw settings_load_error ("Handler \"%s\" has not link to file extension, directory: %s",
							name.c_str(), dirInfo.name.c_str());
					else if (ext.empty())
						ext = info.defaultExtension;


					extList = parseExtensions (ext);
				}


				directory_plugins_list::iterator it = std::find_if (targetPluginsList.begin(), 
						targetPluginsList.end(), PluginInfoByNameFinder(name) );

				if (it == targetPluginsList.end()) 
				{
					regInfo.registrationType = PluginRegistration::Add;
					regInfo.pluginName = name;
					regInfo.pluginIndex = info.pluginIndex;

					if (pluginType == PluginHandler)
					{
						regInfo.processFunc = info.processRequestFunc;
						regInfo.extensions.insert(extList.begin(), extList.end());

						if (!info.defaultExtension.empty()) {
							std::set<string> extList = parseExtensions (info.defaultExtension);
							regInfo.extensions.insert(extList.begin(), extList.end());
						}
					}
					else
					{
                        if (info.global)
                            throw settings_load_error ("Global module registration config cannot be added to directory \"%s\"",
                                dirInfo.name.c_str());
                                            
						std::copy (&info.moduleCallbacks[0], 
							&info.moduleCallbacks[0] + ModuleCallbackTypesCount,
							&regInfo.moduleCallbacks[0]);
					}

					targetPluginsList.push_back ( regInfo );
				
				} else {
					if (pluginType == PluginHandler)
						it->extensions.insert(extList.begin(), extList.end());
					else
						throw settings_load_error ("Module cannot be added twice, directory: %s",
							dirInfo.name.c_str());

				}
				
			} 
			else if (util::equals(item->Value(), SettingsTags::RemoveElement) )
			{
				getAttrRes = item->QueryValueAttribute( SettingsTags::NameAttr, &name);
				if (getAttrRes == TIXML_NO_ATTRIBUTE)
					throw settings_load_error ("<%s> does not have \"%s\" attribute, directory: %s",
						SettingsTags::RegisterElement, SettingsTags::NameAttr, dirInfo.name.c_str());

				if (pluginType == PluginHandler)
				{
					loadStringAttribute (item, SettingsTags::ExtAttr, ext);
					
					if (!ext.empty()) {
						std::set<string> extList = parseExtensions (ext);
						regInfo.extensions.insert(extList.begin(), extList.end());
					}
				}

				regInfo.registrationType = PluginRegistration::Remove;
				regInfo.pluginName = name;
				
				targetPluginsList.push_back ( regInfo );
			}
			else if (util::equals(item->Value(), SettingsTags::ClearElement) )
			{
				regInfo.registrationType = PluginRegistration::Clear;
				
				targetPluginsList.push_back ( regInfo );
			}

		
			item = item->NextSiblingElement ();

			ext.clear();
			name.clear();
			extList.clear();
		}
	}

	void HttpServerSettings::loadDirectoryMappings (class TiXmlElement* mappingsElement, DirectorySettings& dirInfo) 
		throw (settings_load_error)
	{
		
		TiXmlElement* item = mappingsElement->FirstChildElement (SettingsTags::RegisterElement);

		while (item) 
		{
			TiXmlElement* reElem = item->FirstChildElement(SettingsTags::RegexElement);
			if (NULL == reElem)
				throw settings_load_error ("<%s> does not have <%s> child element, directory: %s",
					SettingsTags::RegisterElement, SettingsTags::RegexElement, dirInfo.name.c_str());

			string_constptr re = reElem->GetText();

			if ( aconnect::util::isNullOrEmpty(re) )
				throw settings_load_error ("<%s> element is empty, directory: %s",
					SettingsTags::RegexElement, dirInfo.name.c_str());

			TiXmlElement* urlElem = item->FirstChildElement(SettingsTags::UrlElement);
			if (NULL == urlElem)
				throw settings_load_error ("<%s> does not have <%s> child element, directory: %s",
					SettingsTags::RegisterElement, SettingsTags::UrlElement, dirInfo.name.c_str());


			string_constptr url = urlElem->GetText();
			if ( aconnect::util::isNullOrEmpty(url) )
				throw settings_load_error ("<%s> element is empty, directory: %s",
					SettingsTags::UrlElement, dirInfo.name.c_str());


			dirInfo.mappings.push_back( std::make_pair(boost::regex(re), url));

			item = item->NextSiblingElement (SettingsTags::RegisterElement);
		}


	}

	void HttpServerSettings::initPlugins (PluginType pluginType)
	{
		assert (_logger);

		global_plugins_map::const_iterator iter;
		global_plugins_map& plugins = (pluginType == PluginHandler ? _registeredHandlers : _registeredModules);

		for (iter = plugins.begin(); iter != plugins.end(); ++iter)
		{
			bool inited = reinterpret_cast<init_plugin_function> (iter->second.initFunc) 
				(iter->second.params, iter->second.pluginIndex, this);
			
			if (!inited)
				throw settings_load_error ("Plugin \"%s\" initialization failed failed",
					iter->first.c_str() );
		}
	}

	void HttpServerSettings::destroyPlugins (PluginType pluginType, bool unload)
	{
		if ( !isLoadedCorrectly())
			return;

		global_plugins_map::iterator iter;
		global_plugins_map& plugins = (pluginType == PluginHandler ? _registeredHandlers : _registeredModules);

		for (iter = plugins.begin(); 
			iter != plugins.end(); ++iter)
		{
			assert (iter->second.dll && "DLL is already unloaded");
			if (NULL == iter->second.destroyFunc)
				continue;
			
			reinterpret_cast<destroy_plugin_function> (iter->second.destroyFunc) ();

			if (unload) {
				aconnect::util::unloadLibrary (iter->second.dll);
				iter->second.dll = NULL;
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	//
	//		Helpers
	//
	std::set<string> HttpServerSettings::parseExtensions(string_constref ext) 
	{

		std::set<string> res;
		aconnect::SimpleTokenizer tokens(ext, ";");
		
		for (aconnect::SimpleTokenizer::iterator tok_iter = tokens.begin(); 
				tok_iter != tokens.end(); 
				++tok_iter)
		{
			string token = tok_iter.current_token();
			algo::trim (token);

			if (token.empty())
				continue;

			res.insert (token);
		}

			
		return res;
	}
	

	bool HttpServerSettings::loadIntAttribute (class TiXmlElement* elem, 
		string_constptr attr, int &value) 
	{
		int n;
		if ( elem->QueryIntAttribute (attr, &n) != TIXML_SUCCESS )
			return false;
			
		value = n;
		return true;
	}

	bool HttpServerSettings::loadBoolAttribute (class TiXmlElement* elem, 
		string_constptr attr, bool &value) 
	{
		string_constptr ptr = elem->Attribute (attr);
		if(!ptr)
			return false;

		value = aconnect::util::equals (ptr, SettingsTags::BooleanTrue)
			|| aconnect::util::equals (ptr, SettingsTags::BooleanYes);
		return true;
	}

	bool HttpServerSettings::loadTristateAttribute (class TiXmlElement* elem, 
			string_constptr attr, Tristate::TristateEnum &value)
	{
		bool boolAttr = false;
		if (loadBoolAttribute (elem, attr, boolAttr)) {
			value = boolAttr ? Tristate::True : Tristate::False;
			return true;
		}

		value = Tristate::Undefined;
		return false;
	}
		

	bool HttpServerSettings::loadStringAttribute (class TiXmlElement* elem, 
		string_constptr attr, string &value, bool cannotBeEmpty ) 
	{
		string_constptr ptr = elem->Attribute (attr);
		if(!ptr)
			return false;

		if(cannotBeEmpty && (ptr[0] == '\0'))
			return false;
		
		value = ptr;
		return true;
	}

} // namespace ahttp

