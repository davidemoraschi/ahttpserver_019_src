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

#ifndef AHTTP_SERVER_SETTINGS_H
#define AHTTP_SERVER_SETTINGS_H
#pragma once

#include <stdexcept>
#include <boost/regex.hpp>


#include "aconnect/types.hpp"
#include "aconnect/complex_types.hpp"
#include "aconnect/logger.hpp"
#include "aconnect/server_settings.hpp"

namespace ahttp
{
	using namespace aconnect;

	#include "http_settings_tags.inl"

	namespace Tristate
	{
		enum TristateEnum
		{
			Undefined = -1,
			False = 0,
			True = 1
		};
	};

	namespace PluginRegistration
	{
		enum Type {
			Empty,
			Register,
			Add,
			Remove,
			Clear
		};
	};

	enum PluginType 
	{
		PluginHandler,
		PluginModule
	};

	enum ModuleCallbackType
	{
		ModuleCallbackOnRequestBegin = 0,	// directly after HttpContext::init
		ModuleCallbackOnRequestResolve,		// directory info loaded
		ModuleCallbackOnRequestMapHandler,
		ModuleCallbackOnResponsePreSendHeaders,
		ModuleCallbackOnResponsePreSendContent,
		ModuleCallbackOnResponseEnd
	};

	const int ModuleCallbackTypesCount = 6;
		

	struct PluginRegistrationInfo
	{
		PluginRegistration::Type	registrationType;
		string						pluginName;
		std::set<string>			extensions;
		std::set<string>			excludedExtensions;
		void*						processFunc;	// for handler
		int							pluginIndex;
		void*						moduleCallbacks[ModuleCallbackTypesCount];

		PluginRegistrationInfo() : 
			registrationType (PluginRegistration::Empty),
			processFunc (NULL),
			pluginIndex (-1)
		{
			for (int ndx=0; ndx<ModuleCallbackTypesCount; ++ndx)
				moduleCallbacks[ndx] = NULL;
		}

		inline bool isRequestApplicable (string_constref ext) const
		{
			return (extensions.find (SettingsTags::AllExtensionsMark) != extensions.end() 
						&& excludedExtensions.find (ext) == excludedExtensions.end())
					|| extensions.find (ext) != extensions.end();
		}

	};

	
	struct PluginInfo
	{
		aconnect::library_handle_type	dll;

		string		pathToLoad;
		string		defaultExtension;
		void*		processRequestFunc;
		void*		initFunc;
		void*		destroyFunc;
		int			pluginIndex;
		void*		moduleCallbacks[ModuleCallbackTypesCount];
		bool		global;
		

		aconnect::str2str_map	params;

		PluginInfo() :  
			dll (NULL),
			processRequestFunc (NULL), 
			initFunc (NULL),
			destroyFunc (NULL),
			pluginIndex(0) ,
			global (false)
			{
				for (int ndx=0; ndx<ModuleCallbackTypesCount; ++ndx)
					moduleCallbacks[ndx] = NULL;
			}
	};

	class HttpServerSettings;

	typedef bool (*init_plugin_function) (const aconnect::str2str_map& params, int pluginIndex, 
		HttpServerSettings* globalSettings);
	typedef void (*destroy_plugin_function) ();


	typedef std::map <string, struct DirectorySettings> directories_map;
	typedef std::vector<std::pair<bool, string> > default_documents_vector;
	typedef std::map<ModuleCallbackType, std::list< std::pair <void*, int> > > callback_map;
	typedef std::map<int, callback_map > directories_callback_map;
	
	typedef std::list <PluginRegistrationInfo> directory_plugins_list; 
	
	// key - registered plugin name, value - plugin registartion info
	typedef std::map <string, struct PluginInfo> global_plugins_map;

	typedef std::vector<std::pair<boost::regex, string> > mappings_vector;

	namespace defaults
	{
		const bool EnableKeepAlive		= true;	
		const int KeepAliveTimeout		= 5;	// sec
		const int ServerSocketTimeout	= 900;	// sec
		const int CommandSocketTimeout	= 30;	// sec
		const size_t ResponseBufferSize	= 2 * 1024 * 1024;	// bytes
		const size_t MaxChunkSize				= 65535;	// bytes
		const size_t MaxRequestSize				= 2097152;	// bytes (2 Mb)

		const int UploadCreationTriesCount	= 10;


		string_constant ServerVersion = "ahttpserver";
		string_constant DirectoryConfigFile = "directory.config";
		string_constant MessagesConfigFile = "messages.config";
	}
	
	struct settings_load_error : public std::runtime_error {
		
		settings_load_error (string_constptr format, ...) : std::runtime_error ("") 
		{
			if (NULL == format) {
				message_ = "Settings loading failed";
			} else {
				FORMAT_VA_MESSAGE(format, message);
				message_.swap (message);
			}
		}

		virtual ~settings_load_error() throw () { }
				
		virtual const char * what() const throw () {	
			return (message_.c_str());
		}
	private:
		string message_;
	};
	

	struct DirectorySettings
	{
		DirectorySettings ();
		
		int number;
		string name;
		string parentName;
		string relativePath;	// virtual path from parent
		string virtualPath;		// full virtual path
		string realPath;		// real physical path
		Tristate::TristateEnum browsingEnabled;
		bool isLinkedDirectory;
		string charset;

		default_documents_vector defaultDocuments; // bool - add/remove (false/true)
		
		directory_plugins_list handlers;
		directory_plugins_list modules;

		mappings_vector	mappings;
		
		string headerTemplate,
			directoryTemplate,
			parentDirectoryTemplate,
			virtualDirectoryTemplate,
			fileTemplate,
			footerTemplate;

		size_t maxRequestSize;
		Tristate::TristateEnum enableParentPathAccess; 

		inline bool isParentPathAccessEnabled() const { return enableParentPathAccess == Tristate::True; };
	};


	class HttpServerSettings
	{
	public:
		

		HttpServerSettings();
		~HttpServerSettings();

		void load (string_constptr docPath) throw (settings_load_error);
		
		// properties
		inline aconnect::port_type port() const {
			assert (_port != -1);
			return _port;
		}
		
		inline const string& root() const {
			return _rootDirName;
		}
		
		inline const aconnect::ServerSettings serverSettings() const {
			return _settings;
		}

		inline const string appLocaton() const			{		return _appLocaton;				}
		inline void setAppLocaton (string location)		{		_appLocaton = location;			}

		inline aconnect::Logger* logger()							{		return _logger;					}
		inline void setLogger (aconnect::Logger* logger)			{		_logger = logger;				}

		inline const string serverVersion() const		{		return _serverVersion;			}
		inline void setServerVersion (string version)	{		_serverVersion = version;		}

		inline const aconnect::Log::LogLevel logLevel() const		{		return _logLevel;				}
		inline const string logFileTemplate() const		{		return _logFileTemplate;		}
		inline const size_t	maxLogFileSize() const					{		return _maxLogFileSize;			}
		inline const aconnect::port_type commandPort() const		{		return _commandPort;			}
		
		inline const bool isKeepAliveEnabled() const				{		return _enableKeepAlive;		}
		inline const int keepAliveTimeout() const					{		return _keepAliveTimeout;		}
		inline const int commandSocketTimeout() const				{		return _commandSocketTimeout;	}
		inline const size_t responseBufferSize() const				{		return _responseBufferSize;		}
		inline const size_t maxChunkSize() const					{		return _maxChunkSize;			}
		inline const directories_map& Directories() const			{		return _directories;			}
		inline const string& globalUploadsDirectory() const			{		return _globalUploadsDirectory;		}
		inline const int uploadCreationTriesCount() const			{		return _uploadCreationTriesCount;	}
		inline const bool isLoadedCorrectly() const					{		return _loaded;						}

		
		inline const DirectorySettings& getRootDirSettings() const	{		
			directories_map::const_iterator rootRecord = _directories.find ("/");
 			if (rootRecord == _directories.end()) 
				throw std::runtime_error ("Root web directory (\"/\") is not registered");
			
			return rootRecord->second;			
		}

		const directories_callback_map& modulesCallbacks () const {
			return _modulesCallbacks;
		}

		const DirectorySettings* getDirSettingsByName(string_constref dirName) const;
		
		void updateAppLocationInPath (string &pathStr) const;
		
		string getMimeType (string_constref ext) const;

		void initPlugins (PluginType pluginType);
		void destroyPlugins (PluginType pluginType, bool unload = true);

		static bool loadIntAttribute (class TiXmlElement* elem, 
			string_constptr attr, int &value);
		
		static bool loadBoolAttribute (class TiXmlElement* elem, 
			string_constptr attr, bool &value);

		static bool loadTristateAttribute (class TiXmlElement* elem, 
			string_constptr attr, Tristate::TristateEnum &value);
				
		static bool loadStringAttribute (class TiXmlElement* elem, 
			string_constptr attr, string &value, bool cannotBeEmpty = false);

		static std::set<string> parseExtensions(string_constref ext);

		static void addModuleCallbacksToMap (void** moduleCallbacks, int pluginIndex, callback_map& callbackMap );


		string_constref getMessage (string_constref key) const 
			throw (std::range_error);


	// simple fields
	public:
		// Not used now, reserved to implement slave 
		// processes for directories (like App. Pools in IIS)
		int InstanceId; 
		

	protected:
		void loadServerSettings (class TiXmlElement* serverElem) throw (settings_load_error);
		void loadLoggerSettings (class TiXmlElement* logElement) throw (settings_load_error);
		
		DirectorySettings loadDirectory (class TiXmlElement* dirElement) throw (settings_load_error);

		void tryLoadLocalSettings (string_constref filePath, DirectorySettings& dirInfo) throw (settings_load_error);
		/**
		*	Now loads only plugins, mappings, default-documents
		*/
		void loadLocalDirectorySettings (class TiXmlElement* dirElement, DirectorySettings& dirInfo) throw (settings_load_error);

		void fillDirectoriesMap (std::vector <DirectorySettings>& directoriesList, std::vector <DirectorySettings>::iterator parent);
		void loadMimeTypes (class TiXmlElement* mimeTypesElement) throw (settings_load_error);
		
		void loadPlugins (class TiXmlElement* pluginsElement, PluginType pluginType) throw (settings_load_error);
		string loadPluginRegistration (class TiXmlElement* pluginElem, PluginInfo& info, PluginType pluginType) throw (settings_load_error);

		void copyHandlersRegistration (const DirectorySettings& parent, DirectorySettings& child);

		void copyModulesRegistration (const DirectorySettings& parent, DirectorySettings& child);

		void fillModulesCallbackInfo ();
		
		void loadPluginProperties (string_constref pluginName, PluginInfo& info, PluginType pluginType) 
			throw (settings_load_error);

		void loadGlobalModules (DirectorySettings& root);

		void loadDirectoryIndexDocuments (class TiXmlElement* documentsElement, DirectorySettings& dirInfo) throw (settings_load_error);
		void loadDirectoryPlugins (class TiXmlElement* pluginsElement, 
			DirectorySettings& dirInfo, PluginType type) throw (settings_load_error);
		void loadDirectoryMappings (class TiXmlElement* mappingsElement, DirectorySettings& dirInfo) throw (settings_load_error);

		void initMessages ();
		void loadMessages ();


	protected:
		aconnect::ServerSettings _settings;
		aconnect::port_type _port;
		aconnect::port_type _commandPort;
		string _rootDirName;

		string _appLocaton;
		// logger
		aconnect::Log::LogLevel _logLevel;
		string _logFileTemplate;
		size_t _maxLogFileSize;

		bool _enableKeepAlive;
		int _keepAliveTimeout;

		int _commandSocketTimeout;
		size_t _responseBufferSize;
		size_t _maxChunkSize;

		directories_map _directories;
		aconnect::str2str_map _mimeTypes;
		aconnect::str2str_map _messages;

		aconnect::Logger*	_logger;
		string	_serverVersion;

		global_plugins_map _registeredHandlers;
		global_plugins_map _registeredModules;

		bool				_firstLoad;
		bool				_loaded;

		string	_directoryConfigFile;
		string	_messagesFile;
		string	_globalUploadsDirectory;
		int	_uploadCreationTriesCount;

		// key - directory::number, value - list of callbacks
		directories_callback_map _modulesCallbacks;
	};
}
#endif // AHTTP_SERVER_SETTINGS_H
