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

#ifdef WIN32

#include "vcclr.h"
#include "ahttplib.hpp"
#include "aconnect/util.string.hpp"
#include "AHttp.AspNetWrappers.hpp"

#include "../shared/managed_strings_conversions.inl"

namespace AHttp
{
	using namespace aconnect;
	using namespace ahttp::strings::ServerVariables;

	////////////////////////////////////////////////////////////////////////////////

	[SecurityPermission(SecurityAction::Demand, 
		 Assertion = true,
		 Execution = true,
		 UnmanagedCode = true, 
		 Unrestricted  = true)]
	public ref class Request : public SimpleWorkerRequest
	{
	protected:
		ahttp::HttpContext* _context;
		AspNetHost^ _host;
        array<String^> ^_knownRequestHeaders;
		array<array<String^>^>^ _unknownRequestHeaders;

		bool  _specialCaseStaticFileHeaders;
		String^ _rawUrl;
		String^ _hostName;

		AutoResetEvent^ _requestProcessedEvent;
		

	public:
		Request (AspNetHost^ host, void* context) 
			:  SimpleWorkerRequest (String::Empty, String::Empty, nullptr),
			_context (NULL),
			_host (host),
            _knownRequestHeaders (gcnew array<String^>(RequestHeaderMaximum)),
			_unknownRequestHeaders (nullptr),
			_specialCaseStaticFileHeaders (false),
			_rawUrl (nullptr),
			_hostName (nullptr),
			_requestProcessedEvent (gcnew AutoResetEvent( false ))

		{
			assert (context);
			
			_context = static_cast<ahttp::HttpContext*> (context);
		}
		void Process() 
		{
			HttpRuntime::ProcessRequest(this);
			
			_requestProcessedEvent->WaitOne();
		}

		///////////////////////////////////////////////////////////////////////////////////////////////
        //
        // Implementation of HttpWorkerRequest
        //
        ///////////////////////////////////////////////////////////////////////////////////////////////

		virtual String^ GetUriPath() override{
			return getManagedString ( _context->getServerVariable(ahttp::strings::ServerVariables::URL) );
        }

        virtual String^ GetQueryString() override {
            return getManagedString (_context->QueryString);
        }

		
        virtual array<unsigned char>^ GetQueryStringRawBytes() override 
		{
			if (0 == _context->QueryString.size())
				return gcnew array<unsigned char>(0);

            array<unsigned char>^ res = gcnew array<unsigned char>(_context->QueryString.size());
			pin_ptr<unsigned char> resPtr = &res[0];   

			std::copy (_context->QueryString.begin(), 
				_context->QueryString.end(), 
				(aconnect::char_type*) resPtr);

			return res;
        }
		
        virtual String^ GetRawUrl()  override {
			if (_rawUrl != nullptr)
				return _rawUrl;

			_rawUrl = getManagedString (_context->VirtualPath + (_context->QueryString.size() ? "?" + _context->QueryString : ""));

            return _rawUrl;
        }

        virtual String^ GetHttpVerbName() override {
            return getManagedString (_context->RequestHeader.Method);
        }

        virtual String^ GetHttpVersion() override {
            return getManagedString (_context->getServerVariable(SERVER_PROTOCOL));
        }

		virtual String^ GetRemoteAddress() override {
            return getManagedString (_context->getServerVariable(REMOTE_ADDR));
        }

		virtual int GetRemotePort() override {
            return _context->Client->port;
        }

        virtual String^ GetLocalAddress() override {
			if (_hostName == nullptr) {
				_hostName = getManagedString (_context->RequestHeader.getHeader(ahttp::strings::HeaderHost) );
				int pos = _hostName->IndexOf(":");
				if (pos > -1)
					_hostName = _hostName->Substring (0, pos);

			}

			return _hostName;
        }

        virtual int GetLocalPort() override {
            return _context->Client->server->port();
        }

        virtual String^ GetFilePath() override {
            return getManagedString (_context->getServerVariable(ahttp::strings::ServerVariables::PATH_INFO));
        }
		
        virtual String^ GetFilePathTranslated() override {
		    return getManagedString (_context->getServerVariable(ahttp::strings::ServerVariables::PATH_TRANSLATED));
        }

        virtual String^ GetPathInfo() override {
			return String::Empty;
        }

        virtual String^ GetAppPath() override {
            return getManagedString (_context->CurrentDirectoryInfo->virtualPath);
        }

        virtual String^ GetAppPathTranslated() override  {
            return getManagedString (_context->CurrentDirectoryInfo->realPath);
        }
		
		virtual array<unsigned char>^  GetPreloadedEntityBody() override 
		{
			if (_context->RequestStream.hasBufferedContent()) {
				
				ahttp::string buffer;
				_context->RequestStream.giveBuffer (buffer);

				array<unsigned char>^ res = gcnew array<unsigned char>(buffer.size());
				pin_ptr<unsigned char> resPtr = &res[0];   

				std::copy (buffer.begin(), buffer.end(), (aconnect::char_type*) resPtr);

				return res;
			
			} else {
			
				return gcnew array<unsigned char>(0);
			}
        }

        virtual bool IsEntireEntityBodyIsPreloaded() override 
		{
			assert (_context->RequestStream.getLoadedContentLength() <= _context->RequestHeader.ContentLength);
            
			return _context->RequestStream.getLoadedContentLength() == _context->RequestHeader.ContentLength;
        }

        virtual int ReadEntityBody(array<unsigned char>^ buffer, int size) override {
        	pin_ptr<unsigned char> bufferPtr = &buffer[0];   
			

			int bytesRead = _context->RequestStream.read((aconnect::char_type*) bufferPtr, size);
			
			return bytesRead;
	    }

        virtual String^ GetKnownRequestHeader(int index) override 
		{
            if (_knownRequestHeaders[index] != nullptr)
                return _knownRequestHeaders[index];
            
            
			String^ headerName = GetKnownRequestHeaderName(index);
            _knownRequestHeaders[index] = GetUnknownRequestHeader (headerName);
            
			return _knownRequestHeaders[index];
        }
    
        virtual String^ GetUnknownRequestHeader(String^ name) override {
            string headerName = getUnmanagedString (name);
			if (_context->RequestHeader.hasHeader (headerName))
				return getManagedString (_context->RequestHeader.getHeader (headerName));

            return nullptr;
        }

		
        virtual array< array<String^>^>^  GetUnknownRequestHeaders() override {
            if (_unknownRequestHeaders != nullptr)
				return _unknownRequestHeaders;
			
			// lazy load
			int headersCount = (int) _context->RequestHeader.Headers.size();
			 
			_unknownRequestHeaders = gcnew array< array<String^>^>(headersCount);
            
			aconnect::str2str_map_ci::const_iterator iter = _context->RequestHeader.Headers.begin();
			
			for (int ndx = 0; 
				iter != _context->RequestHeader.Headers.end(); 
				++iter, ++ndx) 
			{
                _unknownRequestHeaders[ndx] = gcnew array<String^>(2);
                _unknownRequestHeaders[ndx][0] = getManagedString (iter->first);
                _unknownRequestHeaders[ndx][1] = getManagedString (iter->second);
            }

			return _unknownRequestHeaders;
        } 
       
		virtual String^ GetServerVariable(String^ name) override 
		{
            string varName = getUnmanagedString (name);
			return getManagedString (_context->getServerVariable (varName.c_str()));
        }
		
		virtual String^ MapPath(String^ path) override 
		{
            // several optimizations
			// Cassini: if (path == nullptr || path->Length == 0 || path->Equals("/")) 
			if (path->Equals("/"))
				return getManagedString (_context->CurrentDirectoryInfo->realPath);
			
			if (path == nullptr || path->Length == 0)
				return getManagedString (_context->FileSystemPath.file_string());
			
			String^ mappedPath = String::Empty;

			if (_context->CurrentDirectoryInfo->virtualPath == "/") {
				if (_host->MapClientScriptFileRequest (path, mappedPath))
					return mappedPath;
			}
			
			return getManagedString (_context->mapPath (getUnmanagedString (path).c_str() ));
        }
		
		virtual void SendStatus(int statusCode, String^ statusDescription) override 
		{
            _context->Response.Header.Status = statusCode;
			_context->Response.Header.setCustomStatusString (getUnmanagedString (statusDescription) );
        }
		
		virtual void SendKnownResponseHeader(int index, String^ value) override {
            
			// Do not throw exception - works faster
			if (_context->Response.isHeadersSent())
                return;

            switch (index) 
			{
				case HttpWorkerRequest::HeaderServer:
				case HttpWorkerRequest::HeaderDate:
				case HttpWorkerRequest::HeaderConnection:
                    // ignore these
                    return;

                // special case headers for static file responses
				// got from Cassini code - MS ASP.NET specific
				case HttpWorkerRequest::HeaderAcceptRanges:
                    if (value == L"bytes") {
                        _specialCaseStaticFileHeaders = true;
                        return;
                    }
                    break;
				case HttpWorkerRequest::HeaderExpires:
				case HttpWorkerRequest::HeaderLastModified:
                    if (_specialCaseStaticFileHeaders)
                        return;
                    break;
            }

			string headerName = getUnmanagedString (GetKnownResponseHeaderName(index));
			_context->Response.Header.setHeader (headerName, getUnmanagedString(value));
        }

        virtual void SendUnknownResponseHeader(String^ name, String^ value) override 
		{
			// Do not throw exception - works faster
			if (!_context->Response.isHeadersSent())
				_context->Response.Header.setHeader (getUnmanagedString (name), getUnmanagedString(value));
        }

        virtual void SendCalculatedContentLength(int contentLength) override {
			// Do not throw exception - works faster
			if (!_context->Response.isHeadersSent())
				_context->Response.Header.setContentLength (contentLength);
		}

	    virtual bool HeadersSent() override {
            return _context->Response.isHeadersSent();
        }
        
        virtual bool IsClientConnected() override {
			return _context->isClientConnected(); 
		}


        virtual void CloseConnection() override {
            _context->closeConnection (true);
        }
		
		virtual void SendResponseFromMemory(array<unsigned char>^ data, int length) override {
            if (length <= 0)
				return;

			if (_context->isClosed())
				return;

			pin_ptr<unsigned char> ptr = &data[0];   

			try {
				_context->Response.write ((char_type*) ptr, length);
			
			} catch (aconnect::socket_error &ex) {
				// writing to socket failed
				_context->Log->debug ("ASPNET: response writing failed, %s", 
						ex.what());
				
				RaiseCommunicationError ();
			}
		}
		
        virtual void SendResponseFromFile(String^ filename, long long  offset, long long length) override 
		{
        	if (length == 0)
                return;

            FileStream^ f = nullptr;

            try {
				f = gcnew FileStream (filename, FileMode::Open, FileAccess::Read, FileShare::Read);
                SendResponseFromFileStream (f, offset, length);
            }
            finally {
                if (f != nullptr)
                    f->Close();
            }
		}

        virtual void SendResponseFromFile(IntPtr handle, long long  offset, long long  length) override 
		{
          	if (length == 0)
                return;

            FileStream^ f = nullptr;

            try {
				f = gcnew FileStream (gcnew Microsoft::Win32::SafeHandles::SafeFileHandle(handle, false), FileAccess::Read);
                SendResponseFromFileStream(f, offset, length);
            }
            finally {
                if (f != nullptr)
                    f->Close();
            }
		}
		
        virtual void FlushResponse(bool finalFlush) override 
		{
			if (_context->isClosed())
				return;

			try {

				if (finalFlush) 
					_context->Response.end();
				else
					_context->Response.flush();
			
			} catch (aconnect::socket_error &ex) {
				
				// writing to socket failed
				_context->Log->debug ("ASPNET: response flushing failed, %s", 
						ex.what());

				RaiseCommunicationError ();
			}
        }
	
        virtual void EndOfRequest() override {
			_requestProcessedEvent->Set();
        }

	private: 
		
		void RaiseCommunicationError ()
		{
			_context->closeConnection(false);
			throw gcnew HttpException(L"Client was disconnected");
		}

		void SendResponseFromFileStream(FileStream^ f, long long offset, long long length)  
		{
          	
			const int maxChunkLength = 64 * 1024;
            long long fileSize = f->Length;

            if (length == -1)
                length = fileSize - offset;
            if (length == 0 || offset < 0 || length > fileSize - offset)
                return;

            if (offset > 0)
				f->Seek(offset, SeekOrigin::Begin);

            if (length <= maxChunkLength) {
                
				array<unsigned char>^ fileBytes = gcnew array<unsigned char> ( (int) length);
                int bytesRead = f->Read (fileBytes, 0, (int) length);
                SendResponseFromMemory(fileBytes, bytesRead);
            
			} else {
				
                array<unsigned char>^ chunk = gcnew array<unsigned char> (maxChunkLength);
                int bytesRemaining = (int) length;

                while (bytesRemaining > 0) 
				{
                    int bytesToRead = (bytesRemaining < maxChunkLength ? bytesRemaining : maxChunkLength);
					int bytesRead = f->Read (chunk, 0, bytesToRead);

                    SendResponseFromMemory(chunk, bytesRead);
                    bytesRemaining -= bytesRead;
                }
				
            }
			
        }
	};
	
	////////////////////////////////////////////////////////////////////////////////

	void AspNetHost::Init (String^ installPath) 
	{

		_physicalClientScriptPath = installPath + L"\\asp.netclientfiles\\";

		FileVersionInfo^  aspNetVersionInfo = FileVersionInfo::GetVersionInfo(HttpRuntime::typeid->Module->FullyQualifiedName); 

		String^ version4parts = aspNetVersionInfo->FileVersion;
		version4parts = version4parts->Substring (0, version4parts->IndexOf(L' '));

		String^ version3parts = version4parts->Substring(0, version4parts->LastIndexOf('.'));
	    
		_clientScriptPathV10 = "/aspnet_client/system_web/" + version4parts->Replace('.', '_') + "/";
		_clientScriptPathV11 = "/aspnet_client/system_web/" + version3parts->Replace('.', '_') + "/";

		
		// start watching for app domain unloading
		_onAppDomainUnload = gcnew EventHandler(this, &AspNetHost::OnAppDomainUnload);
		Thread::GetDomain()->DomainUnload += _onAppDomainUnload;
		
	}

	bool AspNetHost::MapClientScriptFileRequest (String^ path, String^ mappedPath) 
	{

		if (path->StartsWith(_clientScriptPathV10)) {
			mappedPath = _physicalClientScriptPath + path->Substring(_clientScriptPathV10->Length)->Replace("/", "\\");
			return true;
		}
		if (path->StartsWith(_clientScriptPathV11)) {
			mappedPath = _physicalClientScriptPath + path->Substring(_clientScriptPathV11->Length)->Replace("/", "\\");
			return true;
		}

		return false;
	}

	void AspNetHost::ProcessRequest (void* context) 
	{
		assert (context);

		Request^ request = gcnew Request(this, context);
		request->Process();
	}

	void AspNetHost::OnAppDomainUnload(Object^ unusedObject, EventArgs^ unusedEventArgs) 
	{
		Thread::GetDomain()->DomainUnload -= _onAppDomainUnload;
		
		// cleanup
	}
}

#endif // #ifdef WIN32

