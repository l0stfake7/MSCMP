#include <Windows.h>
#include <stdio.h>
#include <assert.h>
#include "steam_api.h"

/**
 * Inject DLL into process.
 *
 * @param[in] process Process handle.
 * @param[in] dllPath DLL path.
 * @return @c false on case of injection failure @c true otherwise.
 */
bool InjectDll(const HANDLE process, const char *const dllPath)
{
	const size_t libPathLen = strlen(dllPath) + 1;
	SIZE_T bytesWritten = 0;

	void *const remoteLibPath = VirtualAllocEx(process, NULL, libPathLen, MEM_COMMIT, PAGE_READWRITE);
	if (!remoteLibPath) {
		return false;
	}

	if (!WriteProcessMemory(process, remoteLibPath, dllPath, libPathLen, &bytesWritten)) {
		VirtualFreeEx(process, remoteLibPath, sizeof(remoteLibPath), MEM_RELEASE);
		return false;
	}

	const HMODULE kernel32dll = GetModuleHandle("Kernel32");
	if (!kernel32dll) {
		VirtualFreeEx(process, remoteLibPath, sizeof(remoteLibPath), MEM_RELEASE);
		return false;
	}

	const FARPROC pfnLoadLibraryA = GetProcAddress(kernel32dll, "LoadLibraryA");
	if (!pfnLoadLibraryA) {
		FreeModule(kernel32dll);
		VirtualFreeEx(process, remoteLibPath, sizeof(remoteLibPath), MEM_RELEASE);
		return false;
	}

	const HANDLE hThread = CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)pfnLoadLibraryA, remoteLibPath, 0, NULL);
	if (!hThread) {
		FreeModule(kernel32dll);
		VirtualFreeEx(process, remoteLibPath, sizeof(remoteLibPath), MEM_RELEASE);
		return false;
	}

	WaitForSingleObject(hThread, INFINITE);
	CloseHandle(hThread);

	FreeModule(kernel32dll);
	VirtualFreeEx(process, remoteLibPath, sizeof(remoteLibPath), MEM_RELEASE);
	return true;
}

//! Steam api wrapper.
struct SteamWrapper
{
	SteamWrapper(void)
	{
	}

	bool Init(void)
	{
		if (!SteamAPI_IsSteamRunning()) {
			MessageBox(NULL, "To run My Summer Car Multiplayer your Steam client must be running.", "Fatal error", MB_ICONERROR);
			return false;
		}

		if (!SteamAPI_Init()) {
			MessageBox(NULL, "Failed to initialize steam.", "Fatal error", MB_ICONERROR);
			return false;
		}
		return true;
	}

	~SteamWrapper(void)
	{
		SteamAPI_Shutdown();
	}
};

const AppId_t GAME_APP_ID = 516750;
const char *const GAME_APP_ID_STR = "516750";


/**
 * Check if operating system we are running on is 64 bit.
 *
 * @return @c true if operating system is 64 bit, @c false otherwise.
 */
bool IsOS64Bit(void)
{
	// As launcher currently is only compiled as 32bit process this check is sufficient.

	BOOL Wow64Process = FALSE;
	if (!IsWow64Process(GetCurrentProcess(), &Wow64Process)) {
		return false;
	}
	return Wow64Process != 0;
}

/**
 * Launcher entry point.
 *
 * @see https://msdn.microsoft.com/en-us/library/windows/desktop/ms633559(v=vs.85).aspx
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	SteamWrapper steam;
	if (!steam.Init()) {
		return 0;
	}

	ISteamApps *const steamApps = SteamApps();

	if (!steamApps->BIsAppInstalled(GAME_APP_ID)) {
		MessageBox(NULL, "To run My Summer Car Multiplayer you need to have installed My Summer Car game.", "Fatal error", MB_ICONERROR);
		return 0;
	}

	char installFolder[MAX_PATH] = { 0 };
	steamApps->GetAppInstallDir(GAME_APP_ID, installFolder, MAX_PATH);

	const char ExecutableName[] = "mysummercar.exe";

	char gameExePath[MAX_PATH] = { 0 };
	sprintf(gameExePath, "%s\\%s", installFolder, ExecutableName);

	STARTUPINFO startupInfo = { 0 };
	PROCESS_INFORMATION processInformation = { 0 };
	startupInfo.cb = sizeof(startupInfo);

	SetEnvironmentVariable("SteamAppID", GAME_APP_ID_STR);

	if (GetFileAttributes(gameExePath) == INVALID_FILE_ATTRIBUTES) {
		MessageBox(NULL, "Unable to find game .exe file.", "Fatal error", MB_ICONERROR);
		return 0;
	}

	if (!CreateProcess(gameExePath, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, installFolder, &startupInfo, &processInformation)) {
		MessageBox(NULL, "Cannot create game process.", "Fatal error", MB_ICONERROR);
		return 0;
	}

	BOOL Wow64Process = FALSE;
	if (!IsWow64Process(processInformation.hProcess, &Wow64Process)) {
		MessageBox(NULL, "Failed to determinate game process architecture.", "Fatal error", MB_ICONERROR);
		TerminateProcess(processInformation.hProcess, 0);
		return 0;
	}

	if (!Wow64Process && IsOS64Bit()) {
		MessageBox(NULL, "Mod does not support 64bit version of the game. To play multiplayer go to steam and opt out of 64bit beta.", "Fatal error", MB_ICONERROR);
		TerminateProcess(processInformation.hProcess, 0);
		return 0;
	}

	char cPath[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, cPath, MAX_PATH);
	char injectorDllPath[MAX_PATH] = { 0 };

	size_t LauncherPathLength = strlen(cPath);
	for (size_t i = LauncherPathLength - 1; i > 0; --i) {
		if (cPath[i] == '\\') {
			cPath[i] = 0;
			break;
		}
	}

	sprintf(injectorDllPath, "%s\\MSCMPInjector.dll", cPath);

	if (GetFileAttributes(injectorDllPath) == INVALID_FILE_ATTRIBUTES) {
		MessageBox(NULL, "Cannot find MSCMPInjector.dll file.", "Error", MB_ICONERROR);
		return 0;
	}

	if (!InjectDll(processInformation.hProcess, injectorDllPath)) {
		const DWORD InjectionError = GetLastError();
		MessageBox(NULL, "Could not inject dll into the game process. Please try launching the game again.", "Fatal error", MB_ICONERROR);
		TerminateProcess(processInformation.hProcess, 0);
		return 0;
	}

	ResumeThread(processInformation.hThread);
	return 1;
}

/* EOF */