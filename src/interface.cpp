#include "DeviceInterface/DeviceInterface.pkg"
#include "ExodusMCPExtension.h"

IExtension* GetExodusMCPExtension(const wchar_t* implementationName, const wchar_t* instanceName, unsigned int moduleID)
{
	return static_cast<IExtension*>(new ExodusMCPExtension(implementationName, instanceName, moduleID));
}

void DeleteExodusMCPExtension(IExtension* extension)
{
	delete static_cast<ExodusMCPExtension*>(extension);
}

#ifdef EX_DLLINTERFACE
extern "C" __declspec(dllexport) unsigned int GetInterfaceVersion()
{
	return EXODUS_INTERFACEVERSION;
}

extern "C" __declspec(dllexport) bool GetExtensionEntry(unsigned int entryNo, IExtensionInfo& entry)
{
	std::wstring copyrightText;
	std::wstring commentsText;
	HMODULE moduleHandle = NULL;
	BOOL getModuleHandleExReturn = GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)GetExtensionEntry, &moduleHandle);
	if (getModuleHandleExReturn != 0)
	{
		std::wstring modulePath = GetModuleFilePath(moduleHandle);
		GetModuleVersionInfoString(modulePath, VERSIONINFOPROPERTY_LEGALCOPYRIGHT, copyrightText);
		GetModuleVersionInfoString(modulePath, VERSIONINFOPROPERTY_COMMENTS, commentsText);
	}

	switch (entryNo)
	{
	case 0:
		entry.SetExtensionSettings(GetExodusMCPExtension, DeleteExodusMCPExtension, L"MCP.ExodusMCPServer", L"ExodusMCPExtension", 1, copyrightText, commentsText, true);
		return true;
	}
	return false;
}
#endif
