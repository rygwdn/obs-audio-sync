#pragma once

#include <string>

// Resource ID for embedded DLL
#define IDR_PLUGIN_DLL 101

// Registry key for OBS Studio
#define OBS_REGISTRY_KEY "SOFTWARE\\OBS Studio"
#define OBS_REGISTRY_VALUE ""

// Default OBS installation path
#define OBS_DEFAULT_PATH "C:\\Program Files\\obs-studio"

// Function declarations
std::string GetOBSInstallPath();
bool DirectoryExists(const std::string &path);
bool FileExists(const std::string &path);
bool ExtractDLLFromResource(const std::string &outputPath, std::string &errorDetails);
bool CopyFileToDestination(const std::string &source, const std::string &destination);
bool CopyDataFiles(const std::string &obsPath, const std::string &pluginName);
bool IsOBSRunning(const std::string &pluginName);
void ShowProgressDialog(const std::wstring &title);
void UpdateProgressDialog(int percentage, const std::wstring &message);
void CloseProgressDialog();
void ShowErrorMessage(const std::wstring &title, const std::wstring &message);
void ShowSuccessMessage(const std::wstring &title, const std::wstring &message);
