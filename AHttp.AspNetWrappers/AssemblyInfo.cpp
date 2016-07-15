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

using namespace System;
using namespace System::Reflection;
using namespace System::Runtime::CompilerServices;
using namespace System::Runtime::InteropServices;
using namespace System::Security;
using namespace System::Security::Permissions;

//
// General Information about an assembly is controlled through the following
// set of attributes. Change these attribute values to modify the information
// associated with an assembly.
//


[assembly:AssemblyTitle(L"AspNetWrappers")];
[assembly:AssemblyDescription(L"ahttp server ASP.NET wrappers set")];
[assembly:AssemblyConfiguration("")]
[assembly:AssemblyCompany(L"a")];
[assembly:AssemblyProduct(L"AHttp.AspNetWrappers")];
[assembly:AssemblyCopyright("Copyright © 2008")];
[assembly:AssemblyTrademark("")];
[assembly:AssemblyCulture("")];


[assembly:AssemblyDelaySign(true)];
[assembly:AssemblyKeyFile(L"AHttp.AspNetWrappers.snk")];

[assembly:ComVisible(false)];
[assembly:CLSCompliant(true)];

[assembly: Guid("1DC6C117-ADFA-4a86-A4AE-2A3EACC9E8ED")];

[assembly:AssemblyVersion(L"1.0.0.0")];
[assembly:AssemblyFileVersion(L"1.0.0.0")];

[assembly:SecurityPermission(SecurityAction::RequestMinimum, 
							 Assertion = true,
							 Execution = true,
							 UnmanagedCode = true, 
							 Unrestricted  = true)];

[assembly:AllowPartiallyTrustedCallers()];


#endif // #ifdef WIN32
