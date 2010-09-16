/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * Main application class that launches
 * everything else
 */

#ifdef _MSC_VER
#include "StdAfx.h"
#endif
#include <sstream>
#include <boost/system/system_error.hpp>

#include "System/Platform/errorhandler.h"
#include "System/Platform/Threading.h"

#ifndef _MSC_VER
#include "StdAfx.h"
#endif
#include "lib/gml/gml.h"
#include "System/LogOutput.h"
#include "System/Exceptions.h"

#include "SpringApp.h"

#ifdef WIN32
#include "Platform/Win/win32.h"
#endif

int g_ret = 0;



void MainFunc(int argc, char** argv) {
#ifdef __MINGW32__
	// For the MinGW backtrace() implementation we need to know the stack end.
	{
		extern void* stack_end;
		char here;
		stack_end = (void*) &here;
	}
#endif

	while (Threading::GetMainThread() == NULL);

#ifdef USE_GML
	set_threadnum(GML_DRAW_THREAD_NUM);
  #if GML_ENABLE_TLS_CHECK
	if (gmlThreadNumber != GML_DRAW_THREAD_NUM) {
		handleerror(NULL, "Thread Local Storage test failed", "GML error:", MBF_OK | MBF_EXCL);
	}
  #endif
#endif

	try  {
		try {
			SpringApp app;
			g_ret = app.Run(argc, argv);
		} CATCH_SPRING_ERRORS
	} catch (boost::thread_interrupted const&) {
		handleerror(NULL, Threading::GetThreadError().what(), "Thread error:", MBF_OK | MBF_EXCL);
	}

	g_ret = -1;
}



int Run(int argc, char* argv[])
{
	boost::thread* mainThread = new boost::thread(boost::bind(&MainFunc, argc, argv));
	Threading::SetMainThread(mainThread);
	mainThread->join();
	delete mainThread;

	return g_ret;
}



/**
 * @brief main
 * @return exit code
 * @param argc argument count
 * @param argv array of argument strings
 *
 * Main entry point function
 */
int main(int argc, char* argv[])
{
	return Run(argc, argv);
}



#ifdef WIN32
int WINAPI WinMain(HINSTANCE hInstanceIn, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	return Run(__argc, __argv);
}
#endif
