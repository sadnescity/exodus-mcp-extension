#ifndef __MCPSERVER_H__
#define __MCPSERVER_H__
#include "ExtensionInterface/ExtensionInterface.pkg"
#include <string>
#include <functional>
#include <map>

// Forward declare to avoid pulling httplib into every translation unit
namespace httplib { class Server; }

class MCPServer
{
public:
	MCPServer(ISystemExtensionInterface& systemInterface, unsigned int port);
	~MCPServer();

	void Run();
	void Stop();

private:
	// MCP protocol handlers
	void HandleInitialize(const std::string& requestBody, std::string& responseBody);
	void HandleToolsList(const std::string& requestBody, std::string& responseBody);
	void HandleToolsCall(const std::string& requestBody, std::string& responseBody);

	// Tool implementations - System
	std::string ToolGetSystemStatus();
	std::string ToolRunSystem();
	std::string ToolStopSystem();

	// Tool implementations - Devices
	std::string ToolListDevices();

	// Tool implementations - CPU
	std::string ToolReadCPURegisters(const std::string& deviceName);
	std::string ToolReadMemory(const std::string& deviceName, unsigned int address, unsigned int length);
	std::string ToolWriteMemory(const std::string& deviceName, unsigned int address, const std::vector<unsigned char>& data);
	std::string ToolDisassemble(const std::string& deviceName, unsigned int address, unsigned int count);

	// Tool implementations - Memory search
	std::string ToolSearchMemory(const std::string& deviceName, unsigned int startAddress, unsigned int endAddress, const std::vector<unsigned char>& pattern);

	// Tool implementations - Breakpoints
	std::string ToolSetBreakpoint(const std::string& deviceName, unsigned int address);
	std::string ToolRemoveBreakpoint(const std::string& deviceName, unsigned int address);
	std::string ToolListBreakpoints(const std::string& deviceName);

	// Tool implementations - Watchpoints
	std::string ToolSetWatchpoint(const std::string& deviceName, unsigned int address, unsigned int size, bool onRead, bool onWrite);
	std::string ToolRemoveWatchpoint(const std::string& deviceName, unsigned int address);
	std::string ToolListWatchpoints(const std::string& deviceName);

	// Tool implementations - VDP raw
	std::string ToolReadVRAM(unsigned int address, unsigned int length);
	std::string ToolReadCRAM(unsigned int address, unsigned int length);
	std::string ToolReadVSRAM(unsigned int address, unsigned int length);
	std::string ToolReadVDPRegisters();

	// Tool implementations - VDP decoded
	std::string ToolReadSpriteTable();
	std::string ToolReadPalette();
	std::string ToolReadNametable(const std::string& plane, unsigned int rowStart, unsigned int rowCount);
	std::string ToolReadVDPState();

	// Tool implementations - Screenshot & pixel info
	std::string ToolScreenshot();
	std::string ToolQueryPixel(unsigned int x, unsigned int y);

	// Tool implementations - Execution
	std::string ToolStepDevice(const std::string& deviceName);

	// Helper functions
	IDevice* FindDeviceByName(const std::string& name);
	static unsigned int ParseAddress(const std::string& str);

private:
	ISystemExtensionInterface& _systemInterface;
	unsigned int _port;
	std::unique_ptr<httplib::Server> _httpServer;
};

#endif
