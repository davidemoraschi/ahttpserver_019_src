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

#ifndef ACONNECT_UTIL_H
#define ACONNECT_UTIL_H

#include <boost/algorithm/string.hpp>
#include <map>

#include "types.hpp"
#include "error.hpp"

namespace aconnect 
{
	template <typename FunT,   // The type of the function being called
		typename ParamT> // The type of its parameter
	struct ThreadProcAdapter 
	{
		ThreadProcAdapter (FunT f, const ParamT& p) : 
	f_(f), p_(p) {}  

	void operator( )( ) { 
		f_(p_);         
	}
	private:
		FunT    f_;
		ParamT	p_;
	};

	typedef void (*simple_callback) ();

	template <typename OnDestroyFun = simple_callback> 
	class ScopedGuard
	{
	public:
		
		ScopedGuard (OnDestroyFun proc) : _proc (proc), _active (true) {  }
		
		~ScopedGuard () {   
			release();
		}

		void release() {
			if (!_active)
				return;

			_active = false;

			try {
				_proc();
			} catch(...) {
				// eat exception
			}
		}

	private:
		OnDestroyFun _proc;
		bool _active;
	};

	template <typename OnDestroyFun, typename Param> 
	class ScopedParamGuard
	{
	public:
		
		ScopedParamGuard (OnDestroyFun proc, Param p) : _proc (proc), _p(p) {  }
		~ScopedParamGuard () {   
			_proc(_p);
		}
	private:
		OnDestroyFun _proc;
		Param _p;
	};

	template <typename T, typename F> 
	class ScopedMemberPointerGuard
	{
	public:
		
		ScopedMemberPointerGuard (T* obj, F T::* member, F initialValue ) : 
		  _obj (obj),
		  _member (member) 
		  {
			  _obj->*_member = initialValue;
		  }
		
		  ~ScopedMemberPointerGuard () {   
			_obj->*_member = 0;	
			
		}
	private:
		T* _obj;
		F T::* _member;
	};

	interface IStopable {
		virtual bool isStopped() = 0;
		inline virtual ~IStopable () {} ;
	};



	// set of utility functions
	namespace util 
	{
		void zeroMemory (void *p, int size); 
		
		unsigned long getCurrentThreadId();
		
		void detachFromConsole() throw (std::runtime_error);

		void unloadLibrary(library_handle_type handle) throw (application_error);

		void* getDllSymbol (library_handle_type handle, string_constptr symbolName, bool mandatory = false) 
			throw (application_error);

		
		template<typename T> T min2 (T n1, T n2) {
			return (n1 < n2 ? n1 : n2);
		};
		template<typename T> T max2 (T n1, T n2) {
			return (n1 > n2 ? n1 : n2);
		};
	}
}


#endif // ACONNECT_UTIL_H

