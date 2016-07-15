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

#ifndef PYTHON_HANDLER_WRAPPERS_H
#define PYTHON_HANDLER_WRAPPERS_H

#include <boost/noncopyable.hpp>
#include <boost/python.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp> 
#include <boost/python/suite/indexing/map_indexing_suite.hpp> 

#include "aconnect/types.hpp"
#include "utility.hpp"

namespace python = boost::python;

//////////////////////////////////////////////////////////////////////////
//
//	Utility
aconnect::string loadPythonError();


//////////////////////////////////////////////////////////////////////////
class TracebackLoaderWrapper : private boost::noncopyable
{
public:
	inline void write (aconnect::string_constptr data)	{ 
		content += data;	
	}
	aconnect::string content;
};



//////////////////////////////////////////////////////////////////////////
// 
//	
class RequestHeaderWrapper : private boost::noncopyable
{
public:
	RequestHeaderWrapper (ahttp::HttpRequestHeader *header) : _header(header) {
		assert (_header);
	}
	~RequestHeaderWrapper()
	{
		_header = NULL;
	}

	inline size_t getLength() {
		return _header->Headers.size(); 
	}
	inline std::string getHeader (aconnect::string_constptr key) const {
		PyThreadStateGuard guard;
		return _header->getHeader(key); 
	}
	inline bool hasHeader (aconnect::string_constptr key) const {
		PyThreadStateGuard guard;
		return _header->hasHeader(key); 
	}
	inline const aconnect::str2str_map& items () {
		PyThreadStateGuard guard;

		if (_items.size() != _header->Headers.size()) {
			_items.clear();
			std::copy (_header->Headers.begin(), 
				_header->Headers.end(),
				std::inserter(_items, _items.begin()));
		}

		return _items; 
	}
	inline std::string requestMethod() const	{ return _header->Method;		}
	inline int requestHttpVerHigh()	const 		{ return _header->VersionHigh;	}
	inline int requestHttpVerLow() const		{ return _header->VersionLow;	}
	inline std::string userAgent() const 		{ PyThreadStateGuard guard;	return _header->getHeader ( ahttp::strings::HeaderUserAgent);	}

protected:
	ahttp::HttpRequestHeader *_header;
	aconnect::str2str_map _items;

};



//////////////////////////////////////////////////////////////////////////
// 
//	
class RequestWrapper : private boost::noncopyable
{
public:
	RequestWrapper (ahttp::HttpContext *context) : 
		  _context(context), 
		  _requestLoaded (false), 
		  requestReadInRawForm_(false)
	{
	  assert (context);
	}
	~RequestWrapper()
	{
		_context = NULL;
	}


	inline const aconnect::str2str_map& getParameters () { 
	  processRequest(); return _context->GetParameters; 
	}
	inline const aconnect::str2str_map& postParameters() { 
	  processRequest(); return _context->PostParameters;
	}
	inline const aconnect::str2str_map& cookies () { 
	  processRequest(); return _context->Cookies; 
	}
	inline const std::map <aconnect::string, ahttp::UploadFileInfo>& files () { 
	  processRequest(); return _context->UploadedFiles; 
	}

	inline bool isRead () {	return _context->RequestStream.isRead(); }
	
	aconnect::string_constptr param (aconnect::string_constptr key);
	
	std::string rawRead (int buffSize);
	
	void processRequest();
	

protected:
	inline void throwRequestReadRawError () {
		PyErr_SetString (PyExc_RuntimeError, "HTTP request has been read in raw form");
		python::throw_error_already_set();
	}
	inline void throwRequestProcessedError () {
		PyErr_SetString (PyExc_RuntimeError, "HTTP request has been loaded to collections: use 'get' or 'post'");
		python::throw_error_already_set();
	}

	ahttp::HttpContext *_context;
	bool _requestLoaded;
	bool requestReadInRawForm_;

};


//////////////////////////////////////////////////////////////////////////
// 
//	wrapper for ahttp::HttpContext - cover some HttpContext functionality
class HttpContextWrapper : private boost::noncopyable
{
public:
	HttpContextWrapper (ahttp::HttpContext *context) : 
		_context (context),
		_contentWritten (false), 
		_requestHeader (context ? &context->RequestHeader : NULL),
		_request (context) 
	  {
		  assert (context);
	  }

	~HttpContextWrapper()
	{
		_context = NULL;
	}

	// request info
	inline std::string path() {
	  return _context->RequestHeader.Path; 
	}
	inline std::string virtualPath() {
	  return _context->VirtualPath; 
	}
	inline std::string initialVirtualPath() {
	  return _context->InitialVirtualPath; 
	}
	inline std::string scriptPath() {
	  return _context->FileSystemPath.string(); 
	}

	// client info
	inline std::string clientIpAddr() {
	  return aconnect::util::formatIpAddr (_context->Client->ip); 
	}
	inline aconnect::port_type clientPort() {
	  return _context->Client->port; 
	}
	inline aconnect::port_type serverPort () {
	  return _context->Client->server->port(); 
	}
	//////////////////////////////////////////////////////////////////////////

	inline bool isClientConnected () {
	  return _context->isClientConnected();
	}

	inline void processServerError (aconnect::string_constptr message) {
		PyThreadStateGuard guard;
		return ahttp::HttpServer::processServerError (*_context, ahttp::HttpStatus::InternalServerError, message);
	}

	inline std::string getServerVariable (aconnect::string_constptr varName) {
		PyThreadStateGuard guard;
		return _context->getServerVariable (varName);
	}

	inline aconnect::string mapPath (aconnect::string_constptr virtPath) {
		PyThreadStateGuard guard;
		return _context->mapPath (virtPath);
	}

	//////////////////////////////////////////////////////////////////////////
	//
	//	response modification

	void write (aconnect::string_constptr data);
	void writeEscaped (aconnect::string_constptr data);
	void flush ();
	void setContentType (aconnect::string_constptr contentType, aconnect::string_constptr charset="");

	inline void setUtf8Html () {
	  return setContentType (ahttp::strings::ContentTypeTextHtml, ahttp::strings::ContentCharsetUtf8);
	}

	inline void setResponseHeader (aconnect::string_constptr header, aconnect::string_constptr value) {
		_context->Response.Header.setHeader (header, value);
	}

	inline int getResponseStatus () {
		return _context->Response.Header.Status;
	}

	inline void setResponseStatus (int status) {
		_context->Response.Header.Status = status;
	}
	

protected:	
	ahttp::HttpContext *_context;
	bool _contentWritten;

public:
	RequestHeaderWrapper _requestHeader;
	RequestWrapper _request;

};

#endif // PYTHON_HANDLER_WRAPPERS_H

