#include "ExodusMCPExtension.h"

//----------------------------------------------------------------------------------------------------------------------
// Constructors
//----------------------------------------------------------------------------------------------------------------------
ExodusMCPExtension::ExodusMCPExtension(const std::wstring& implementationName, const std::wstring& instanceName, unsigned int moduleID)
:Extension(implementationName, instanceName, moduleID), _running(false), _port(8600)
{ }

//----------------------------------------------------------------------------------------------------------------------
ExodusMCPExtension::~ExodusMCPExtension()
{
	StopServer();
}

//----------------------------------------------------------------------------------------------------------------------
// Initialization functions
//----------------------------------------------------------------------------------------------------------------------
bool ExodusMCPExtension::BuildExtension()
{
	StartServer();
	return true;
}

//----------------------------------------------------------------------------------------------------------------------
// Settings
//----------------------------------------------------------------------------------------------------------------------
void ExodusMCPExtension::LoadSettingsState(IHierarchicalStorageNode& node)
{
	std::list<IHierarchicalStorageNode*> childList = node.GetChildList();
	for (IHierarchicalStorageNode* child : childList)
	{
		if (child->GetName() == L"MCPPort")
		{
			child->ExtractData(_port);
		}
	}
}

//----------------------------------------------------------------------------------------------------------------------
void ExodusMCPExtension::SaveSettingsState(IHierarchicalStorageNode& node) const
{
	node.CreateChild(L"MCPPort").SetData(_port);
}

//----------------------------------------------------------------------------------------------------------------------
// Server management
//----------------------------------------------------------------------------------------------------------------------
void ExodusMCPExtension::StartServer()
{
	if (_running)
		return;

	_running = true;
	_server = std::make_unique<MCPServer>(GetSystemInterface(), _port);
	_serverThread = std::thread([this]()
	{
		_server->Run();
	});
}

//----------------------------------------------------------------------------------------------------------------------
void ExodusMCPExtension::StopServer()
{
	if (!_running)
		return;

	_running = false;
	if (_server)
	{
		_server->Stop();
	}
	if (_serverThread.joinable())
	{
		// Give the server thread a moment to exit, then detach if it's stuck
		auto future = std::async(std::launch::async, [this]() { _serverThread.join(); });
		if (future.wait_for(std::chrono::seconds(2)) == std::future_status::timeout)
		{
			_serverThread.detach();
		}
	}
	_server.reset();
}
