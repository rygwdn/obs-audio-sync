#include "installer.h"

#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

// Progress dialog interface
IProgressDialog *g_pProgressDialog = nullptr;

std::string GetOBSInstallPath()
{
	std::string obsPath;

	// Try registry first
	HKEY hKey;
	LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, OBS_REGISTRY_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &hKey);

	if (result == ERROR_SUCCESS) {
		char path[MAX_PATH] = {0};
		DWORD pathSize = sizeof(path);
		DWORD type = REG_SZ;

		result = RegQueryValueExA(hKey, OBS_REGISTRY_VALUE, NULL, &type, (LPBYTE)path, &pathSize);

		RegCloseKey(hKey);

		if (result == ERROR_SUCCESS && pathSize > 0) {
			obsPath = path;
			// Remove trailing backslash if present
			if (!obsPath.empty() && obsPath.back() == '\\') {
				obsPath.pop_back();
			}
		}
	}

	// If registry lookup failed, try default path
	if (obsPath.empty()) {
		obsPath = OBS_DEFAULT_PATH;
	}

	return obsPath;
}

bool DirectoryExists(const std::string &path)
{
	DWORD dwAttrib = GetFileAttributesA(path.c_str());
	return (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

bool FileExists(const std::string &path)
{
	DWORD dwAttrib = GetFileAttributesA(path.c_str());
	return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

bool ExtractDLLFromResource(const std::string &outputPath)
{
	// Find the embedded DLL resource
	HRSRC hRes = FindResourceA(NULL, MAKEINTRESOURCEA(IDR_PLUGIN_DLL), (LPCSTR)RT_RCDATA);
	if (!hRes) {
		DWORD error = GetLastError();
		// Log error for debugging (can be viewed in Event Viewer or debugger)
		// Error 1813 = ERROR_RESOURCE_TYPE_NOT_FOUND
		// Error 1814 = ERROR_RESOURCE_NAME_NOT_FOUND
		return false;
	}

	// Load the resource
	HGLOBAL hData = LoadResource(NULL, hRes);
	if (!hData) {
		return false;
	}

	// Lock the resource
	LPVOID pData = LockResource(hData);
	if (!pData) {
		return false;
	}

	// Get resource size
	DWORD size = SizeofResource(NULL, hRes);
	if (size == 0) {
		return false;
	}

	// Write to temporary file
	std::ofstream outFile(outputPath, std::ios::binary);
	if (!outFile) {
		return false;
	}

	outFile.write(static_cast<const char *>(pData), size);
	outFile.close();

	return outFile.good();
}

bool CopyFileToDestination(const std::string &source, const std::string &destination)
{
	// Create destination directory if it doesn't exist
	std::string destDir = destination;
	size_t lastSlash = destDir.find_last_of("\\/");
	if (lastSlash != std::string::npos) {
		destDir = destDir.substr(0, lastSlash);
		CreateDirectoryA(destDir.c_str(), NULL);
	}

	// Copy file (overwrite if exists)
	return CopyFileA(source.c_str(), destination.c_str(), FALSE) != 0;
}

bool CopyDataFiles(const std::string &obsPath, const std::string &pluginName)
{
	// Check if data directory exists in installer's directory
	// The installer might be run from a temporary location, so we need to find the data directory
	char exePath[MAX_PATH];
	GetModuleFileNameA(NULL, exePath, MAX_PATH);
	std::string exeDir = exePath;
	size_t lastSlash = exeDir.find_last_of("\\/");
	if (lastSlash != std::string::npos) {
		exeDir = exeDir.substr(0, lastSlash);
	}

	std::string sourceDataDir = exeDir + "\\data";
	if (!DirectoryExists(sourceDataDir)) {
		// Try relative to current directory
		sourceDataDir = "data";
		if (!DirectoryExists(sourceDataDir)) {
			// No data files to copy - this is not an error
			return true;
		}
	}

	std::string destDataDir = obsPath + "\\data\\obs-plugins\\" + pluginName;

	// Create destination directory structure
	std::string destParent = destDataDir;
	size_t lastBackslash = destParent.find_last_of("\\/");
	while (lastBackslash != std::string::npos) {
		destParent = destParent.substr(0, lastBackslash);
		CreateDirectoryA(destParent.c_str(), NULL);
		lastBackslash = destParent.find_last_of("\\/");
	}
	CreateDirectoryA(destDataDir.c_str(), NULL);

	// Copy directory recursively using SHFileOperation
	SHFILEOPSTRUCTA fileOp = {0};
	// SHFileOperation requires double-null-terminated strings
	std::string sourceWithNull = sourceDataDir;
	if (sourceWithNull.back() != '\\') {
		sourceWithNull += "\\";
	}
	sourceWithNull += "\0";

	std::string destWithNull = destDataDir;
	if (destWithNull.back() != '\\') {
		destWithNull += "\\";
	}
	destWithNull += "\0";

	fileOp.hwnd = NULL;
	fileOp.wFunc = FO_COPY;
	fileOp.pFrom = sourceWithNull.c_str();
	fileOp.pTo = destWithNull.c_str();
	fileOp.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_SILENT;

	int result = SHFileOperationA(&fileOp);
	return result == 0;
}

bool IsOBSRunning(const std::string &pluginName)
{
	// Check if OBS is running by checking for the DLL lock
	// Try to open the plugin DLL in the OBS plugins directory
	// If it's locked, OBS is likely running
	std::string obsPath = GetOBSInstallPath();
	if (obsPath.empty()) {
		return false; // Can't check if OBS path not found
	}

	std::string pluginDllPath = obsPath + "\\obs-plugins\\64bit\\" + pluginName + ".dll";

	// Try to open the file with exclusive access
	HANDLE hFile = CreateFileA(pluginDllPath.c_str(), GENERIC_READ | GENERIC_WRITE,
				   0, // No sharing - this will fail if file is in use
				   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile != INVALID_HANDLE_VALUE) {
		CloseHandle(hFile);
		return false; // File is not locked, OBS is not running
	}

	// Check error - if sharing violation, file is locked (OBS is running)
	DWORD error = GetLastError();
	return error == ERROR_SHARING_VIOLATION;
}

void ShowProgressDialog(const std::wstring &title)
{
	// Initialize COM
	CoInitialize(NULL);

	// Create progress dialog
	HRESULT hr = CoCreateInstance(CLSID_ProgressDialog, NULL, CLSCTX_INPROC_SERVER, IID_IProgressDialog,
				      (void **)&g_pProgressDialog);

	if (SUCCEEDED(hr) && g_pProgressDialog) {
		g_pProgressDialog->SetTitle(title.c_str());
		g_pProgressDialog->SetCancelMsg(L"Cancelling installation...", NULL);
		g_pProgressDialog->StartProgressDialog(NULL, NULL, PROGDLG_NORMAL, NULL);
	}
}

void UpdateProgressDialog(int percentage, const std::wstring &message)
{
	if (g_pProgressDialog) {
		g_pProgressDialog->SetLine(1, message.c_str(), FALSE, NULL);
		g_pProgressDialog->SetProgress(percentage, 100);
	}
}

void CloseProgressDialog()
{
	if (g_pProgressDialog) {
		g_pProgressDialog->StopProgressDialog();
		g_pProgressDialog->Release();
		g_pProgressDialog = nullptr;
	}
	CoUninitialize();
}

void ShowErrorMessage(const std::wstring &title, const std::wstring &message)
{
	MessageBoxW(NULL, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
}

void ShowSuccessMessage(const std::wstring &title, const std::wstring &message)
{
	MessageBoxW(NULL, message.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}
