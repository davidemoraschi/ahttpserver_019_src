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

#include <boost/scoped_array.hpp>
#include <boost/thread.hpp>

#include "ahttplib.hpp"
#include "aconnect/util.string.hpp"

#include "wrappers.hpp"

//////////////////////////////////////////////////////////////////////////
// 
//	
aconnect::string_constptr RequestWrapper::param (aconnect::string_constptr key)  
{
	processRequest();

	PyThreadStateGuard guard;

	aconnect::str2str_map::const_iterator iter;
	if ((iter = _context->GetParameters.find(key)) != _context->GetParameters.end())
		return iter->second.c_str();

	if ((iter = _context->PostParameters.find(key)) != _context->PostParameters.end())
		return iter->second.c_str();

	if ((iter = _context->Cookies.find(key)) != _context->Cookies.end())
		return iter->second.c_str();

	return NULL;
}

std::string RequestWrapper::rawRead (int buffSize)	{ 

	if (_requestLoaded) 
		throwRequestProcessedError ();

	requestReadInRawForm_ = true;

	PyThreadStateGuard stateGuard;

	boost::scoped_array<aconnect::char_type> buff (new aconnect::char_type [buffSize]);
	int bytesRead = _context->RequestStream.read (buff.get(), buffSize);

	return std::string (buff.get(), bytesRead);
}

void RequestWrapper::processRequest() 
{
	if (requestReadInRawForm_)
		return throwRequestReadRawError ();

	if (_requestLoaded)
		return;
	
	PyThreadStateGuard stateGuard;

	// load request data
	_context->parseQueryStringParams ();
	_context->parseCookies();
	_context->loadPostParams ();
	
	_requestLoaded = true;
}

//////////////////////////////////////////////////////////////////////////
// 
//	wrapper for ahttp::HttpContext - cover some HttpContext functionality
void HttpContextWrapper::write (aconnect::string_constptr data) 
{
	assert (_context);

	PyThreadStateGuard stateGuard;

	if (!_contentWritten)
	{
		_contentWritten = true;
		_context->setHtmlResponse();
	}

	_context->Response.write (data);
}

void HttpContextWrapper::writeEscaped (aconnect::string_constptr data) 
{
	write ( aconnect::util::escapeHtml(data).c_str() );
}

void HttpContextWrapper::flush () 
{
	PyThreadStateGuard stateGuard;

	assert (_context);

	_context->Response.flush();
}

void HttpContextWrapper::setContentType (aconnect::string_constptr contentType, aconnect::string_constptr charset) 
{
	assert (_context);
	
	if (_contentWritten)
	{
		PyErr_SetString (PyExc_RuntimeError, "HTTP response header cannot be set - response content writing already started");
		python::throw_error_already_set();
	}

	_context->Response.Header.setContentType (contentType, charset);
}

//////////////////////////////////////////////////////////////////////////
//
//	Utility
namespace Globals
{
	extern boost::mutex LoadMutex;
}

aconnect::string loadPythonError()
{
	using namespace python;
	
	//PyThreadStateGuard stateGuard;
		
	aconnect::string errorDesc = "Undefined Python exception caught";
	PyObject* type = NULL, 
		*value = NULL, 
		*traceback = NULL;

	try {

		if ( !PyErr_Occurred ())
			return errorDesc;

		PyErr_Fetch (&type, &value, &traceback);
		PyErr_NormalizeException (&type, &value, &traceback);
		
		PyErr_Clear();

		if (!type || !value)
			return errorDesc;

		aconnect::string_constptr format  = "Python: exception caught, type: %s\n"
			"Exception value: %s\n";

		str info (format % make_tuple ( handle<> (type), handle<> (value) ) );
		errorDesc = extract<aconnect::string> ( info );

		if (traceback && traceback != Py_None) {

			reference_existing_object::apply<TracebackLoaderWrapper*>::type converter;
			TracebackLoaderWrapper loader;

			handle<> loaderHandle ( converter( &loader ) );
			object tracebackLoader = object( loaderHandle );

			PyTraceBack_Print(traceback, loaderHandle.get() );
			errorDesc += extract<aconnect::string> ( str (tracebackLoader.attr ("content") ) );
		} 

	} catch (...) {
		errorDesc = "Python exception description loading failed";
	}

	return errorDesc;
}

