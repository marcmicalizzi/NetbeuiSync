#include "syncdirectory.h"
#include <windows.h>
#include <Winnetwk.h>
#include <tchar.h>
#include <iphlpapi.h>
#include <ras.h>
#include <raserror.h>
#include <iostream>
#include <vector>
#include <sstream>
#include <strsafe.h>
#include <list>
#include <mutex>
#include <condition_variable>
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "IPHLPAPI.lib")
#pragma comment(lib, "mpr.lib")

//#define RUNASSERVICE

SERVICE_STATUS        g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
std::mutex mutex;
std::condition_variable cond;

VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv);
VOID WINAPI ServiceCtrlHandler(DWORD);

#define SERVICE_NAME  _T("NetbeuiSync")

int _tmain(int argc, TCHAR *argv[])
{
#ifdef RUNASSERVICE
	SERVICE_TABLE_ENTRY ServiceTable[] =
	{
		{ SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain },
		{ NULL, NULL }
	};

	if (StartServiceCtrlDispatcher(ServiceTable) == FALSE)
	{
		return GetLastError();
	}
#else
	ServiceMain(0, 0);
#endif
	return 0;
}


VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv)
{
#ifdef RUNASSERVICE
	DWORD Status = E_FAIL;

	g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);

	if (g_StatusHandle == NULL)
	{
		return;
	}

	// Tell the service controller we are starting
	ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
	g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwServiceSpecificExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{

	}


	// Tell the service controller we are started
	g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
	}
#endif
	std::vector<SyncDirectory*> dirs;
	std::vector<NETRESOURCE> resources;
	// Start the thread that will perform the main task of the service
	wchar_t localname[3] = L"G:";
	WIN32_FIND_DATA ffd;
	HANDLE hfd = FindFirstFile(L"C:\\sync\\*", &ffd);
	if (hfd != INVALID_HANDLE_VALUE) {
		do {
			if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
			if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				std::wcerr << L"Found computer name " << ffd.cFileName << std::endl;
				WIN32_FIND_DATA cfd;
				std::wstring path = L"C:\\sync\\" + (std::wstring(ffd.cFileName) + L"\\*");
				std::wcerr << L"Searching " << path.c_str() << std::endl;
				HANDLE hcfd = FindFirstFile(path.c_str(), &cfd);
				if (hcfd != INVALID_HANDLE_VALUE) {
					do {
						if (wcscmp(cfd.cFileName, L".") == 0 || wcscmp(cfd.cFileName, L"..") == 0) continue;
						if (cfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
							std::wcerr << L"Found share name " << cfd.cFileName << std::endl;
							if (localname[0] < L'Z') {
								NETRESOURCE nr;

								memset(&nr, 0, sizeof(NETRESOURCE));
								nr.dwType = RESOURCETYPE_ANY;
								wchar_t* ln = new wchar_t[3];
								wcscpy_s(ln, 3, localname);
								nr.lpLocalName = ln;
								localname[0]++;
								std::wstring remote = (std::wstring(L"\\\\") + ffd.cFileName + L"\\" + cfd.cFileName);
								wchar_t* rn = new wchar_t[remote.length() + 1];
								wcscpy_s(rn, remote.length() + 1, remote.c_str());
								nr.lpRemoteName = rn;
								nr.lpProvider = 0;
								std::wstring localpath = L"C:\\sync\\" + (std::wstring(ffd.cFileName)) + L"\\" + (std::wstring(cfd.cFileName)) + L"\\";
								dirs.push_back(new SyncDirectory(nr, localpath));
							}
						}
					} while (FindNextFile(hcfd, &cfd) != 0);
					FindClose(hcfd);
				}
				else {
					std::wcerr << L"No shares found" << std::endl;
				}
			}
		} while (FindNextFile(hfd, &ffd) != 0);
		FindClose(hfd);
	}
	else {
		std::wcerr << L"Unable to open sync directory" << std::endl;
	}

	// Wait until our worker thread exits effectively signaling that the service needs to stop
	std::unique_lock<std::mutex> lock(mutex);
	cond.wait(lock);

	for (size_t i = 0; i < dirs.size(); i++) {
		delete dirs[i];
		dirs[i] = 0;
	}
	/*
	* Perform any cleanup tasks
	*/
#ifdef RUNASSERVICE
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 3;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
	}
#endif
	return;
}


VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode)
{
	switch (CtrlCode)
	{
	case SERVICE_CONTROL_STOP:

		if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
			break;

		/*
		* Perform tasks neccesary to stop the service here
		*/

		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
		g_ServiceStatus.dwWin32ExitCode = 0;
		g_ServiceStatus.dwCheckPoint = 4;

		if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
		{
		}

		// This will signal the worker thread to start shutting down
		cond.notify_all();

		break;

	default:
		break;
	}
}
