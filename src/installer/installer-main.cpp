#include "installer.h"

#include <windows.h>
#include <string>
#include <sstream>

// Get plugin name from build system (will be defined by CMake)
#ifndef PLUGIN_NAME
#define PLUGIN_NAME "obs-audio-sync"
#endif

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	// Initialize
	std::string pluginName = PLUGIN_NAME;
	std::string pluginDLL = pluginName + ".dll";
	int exitCode = 0;

	// Show progress dialog
	ShowProgressDialog(L"Installing OBS Audio Sync Plugin");

	try {
		// Step 1: Find OBS Studio installation
		UpdateProgressDialog(0, L"Finding OBS Studio installation...");
		std::string obsPath = GetOBSInstallPath();

		// Verify OBS installation exists
		if (!DirectoryExists(obsPath)) {
			UpdateProgressDialog(0, L"Error: OBS Studio not found");
			CloseProgressDialog();
			ShowErrorMessage(L"Installation Failed",
					 L"OBS Studio installation not found.\n\n"
					 L"Please install OBS Studio first, or manually copy the plugin files.");
			return 1;
		}

		// Verify OBS is not running (check after we know the path)
		UpdateProgressDialog(10, L"Checking if OBS Studio is running...");
		if (IsOBSRunning(pluginName)) {
			UpdateProgressDialog(0, L"Error: OBS Studio is running");
			CloseProgressDialog();
			ShowErrorMessage(L"Installation Failed",
					 L"OBS Studio is running. Please close OBS Studio and try again.");
			return 1;
		}

		// Step 2: Extract DLL from embedded resource
		UpdateProgressDialog(33, L"Extracting plugin files...");
		char tempPath[MAX_PATH];
		DWORD tempPathLen = GetTempPathA(MAX_PATH, tempPath);
		if (tempPathLen == 0 || tempPathLen >= MAX_PATH) {
			UpdateProgressDialog(0, L"Error: Failed to get temp directory");
			CloseProgressDialog();
			ShowErrorMessage(L"Installation Failed", L"Failed to get temporary directory path.");
			return 1;
		}
		std::string tempDLL = std::string(tempPath) + pluginDLL;
		std::string extractError;
		if (!ExtractDLLFromResource(tempDLL, extractError)) {
			UpdateProgressDialog(0, L"Error: Failed to extract plugin files");
			CloseProgressDialog();
			std::wstringstream errorMsg;
			errorMsg << L"Failed to extract plugin files from installer.\n\n";
			if (!extractError.empty()) {
				// Convert error details to wide string
				int size_needed = MultiByteToWideChar(CP_UTF8, 0, extractError.c_str(),
								      static_cast<int>(extractError.length()), NULL, 0);
				if (size_needed > 0) {
					std::wstring errorDetailsW(size_needed, 0);
					MultiByteToWideChar(CP_UTF8, 0, extractError.c_str(),
							    static_cast<int>(extractError.length()), &errorDetailsW[0],
							    size_needed);
					errorMsg << errorDetailsW;
				} else {
					// Fallback: convert using ANSI if UTF-8 fails
					size_needed = MultiByteToWideChar(CP_ACP, 0, extractError.c_str(),
									  static_cast<int>(extractError.length()), NULL,
									  0);
					if (size_needed > 0) {
						std::wstring errorDetailsW(size_needed, 0);
						MultiByteToWideChar(CP_ACP, 0, extractError.c_str(),
								    static_cast<int>(extractError.length()),
								    &errorDetailsW[0], size_needed);
						errorMsg << errorDetailsW;
					} else {
						errorMsg << L"Error details could not be converted to display format.";
					}
				}
			} else {
				errorMsg << L"Unknown error occurred during extraction.";
			}
			ShowErrorMessage(L"Installation Failed", errorMsg.str());
			return 1;
		}

		// Step 3: Install DLL to OBS
		UpdateProgressDialog(66, L"Installing plugin to OBS Studio...");
		std::string targetDLL = obsPath + R"(\obs-plugins\64bit\)" + pluginDLL;

		if (!CopyFileToDestination(tempDLL, targetDLL)) {
			DWORD error = GetLastError();
			std::wstringstream errorMsg;
			errorMsg << L"Failed to copy plugin to OBS Studio.\n\n";

			if (error == ERROR_ACCESS_DENIED) {
				errorMsg << L"Access denied. You may need administrator rights to install the plugin.";
			} else {
				errorMsg << L"Error code: " << error;
			}

			UpdateProgressDialog(0, L"Error: Installation failed");
			CloseProgressDialog();
			ShowErrorMessage(L"Installation Failed", errorMsg.str());

			// Clean up temp file
			DeleteFileA(tempDLL.c_str());
			return 1;
		}

		// Step 4: Copy data files (if any)
		UpdateProgressDialog(80, L"Copying data files...");
		if (!CopyDataFiles(obsPath, pluginName)) {
			// Data file copy failure is not critical, just log it
			// Continue with installation
		}

		// Step 5: Clean up temp file
		DeleteFileA(tempDLL.c_str());

		// Success!
		UpdateProgressDialog(100, L"Installation complete!");
		CloseProgressDialog();
		ShowSuccessMessage(L"Installation Complete",
				   L"OBS Audio Sync Plugin has been successfully installed to:\n" +
					   std::wstring(obsPath.begin(), obsPath.end()) +
					   L"\n\n"
					   L"Please restart OBS Studio to use the plugin.");
		exitCode = 0;

	} catch (...) {
		UpdateProgressDialog(0, L"Error: Unexpected error occurred");
		CloseProgressDialog();
		ShowErrorMessage(L"Installation Failed", L"An unexpected error occurred during installation.");
		exitCode = 1;
	}

	return exitCode;
}
