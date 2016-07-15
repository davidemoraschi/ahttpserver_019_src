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

#ifndef AHTTP_ASPNET_WRAPPERS_HPP
#define AHTTP_ASPNET_WRAPPERS_HPP




namespace AHttp
{
	using namespace System;
	using namespace System::IO;
	using namespace System::Threading;
	using namespace System::Web;
	using namespace System::Web::Hosting;
	using namespace System::Diagnostics;
	
	using namespace System::Runtime::Remoting::Lifetime;
	using namespace System::Security;
	using namespace System::Security::Permissions;
	using namespace System::Security::Policy;
	using namespace System::Reflection;

	public ref class AspNetHost : MarshalByRefObject
	{



	public:
		
		AspNetHost()
		{}
		
		
	
		virtual Object^ InitializeLifetimeService() override
		{
			return nullptr;
		}

		void Init (String^ installPath);

		bool MapClientScriptFileRequest (String^ path, String^ mappedPath); 
		
		void ProcessRequest (void* context);

		AppDomain^ GetDomain()	{	return AppDomain::CurrentDomain; }
	
		void OnAppDomainUnload(Object^ unusedObject, EventArgs^ unusedEventArgs);


	protected:
		String^ _physicalClientScriptPath;
		String^ _clientScriptPathV10;
		String^ _clientScriptPathV11;
		
		EventHandler^ _onAppDomainUnload;
		
	};
}


#endif