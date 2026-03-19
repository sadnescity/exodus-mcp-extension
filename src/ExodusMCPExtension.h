#ifndef __EXODUSMCPEXTENSION_H__
#define __EXODUSMCPEXTENSION_H__
#include "Extension/Extension.pkg"
#include "MCPServer.h"
#include <thread>
#include <atomic>
#include <future>

class ExodusMCPExtension :public Extension
{
public:
	// Constructors
	ExodusMCPExtension(const std::wstring& implementationName, const std::wstring& instanceName, unsigned int moduleID);
	~ExodusMCPExtension();

	// Initialization functions
	virtual bool BuildExtension() override;

	// Settings
	virtual void LoadSettingsState(IHierarchicalStorageNode& node) override;
	virtual void SaveSettingsState(IHierarchicalStorageNode& node) const override;

private:
	void StartServer();
	void StopServer();

private:
	std::unique_ptr<MCPServer> _server;
	std::thread _serverThread;
	std::atomic<bool> _running;
	unsigned int _port;
};

#endif
