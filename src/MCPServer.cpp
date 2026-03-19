// httplib must be included before Exodus SDK headers to get correct Winsock include order
#include "httplib.h"
#include "nlohmann/json.hpp"

#include "MCPServer.h"
#include "Processor/Processor.pkg"
#include "315-5313/IS315_5313.h"
#include "M68000/IM68000.h"
#include "Image/Image.pkg"
#include "Stream/Stream.pkg"

using json = nlohmann::json;

//----------------------------------------------------------------------------------------------------------------------
// Constructors
//----------------------------------------------------------------------------------------------------------------------
MCPServer::MCPServer(ISystemExtensionInterface& systemInterface, unsigned int port)
:_systemInterface(systemInterface), _port(port)
{ }

//----------------------------------------------------------------------------------------------------------------------
MCPServer::~MCPServer()
{
	Stop();
}

//----------------------------------------------------------------------------------------------------------------------
// Server lifecycle
//----------------------------------------------------------------------------------------------------------------------
void MCPServer::Run()
{
	_httpServer = std::make_unique<httplib::Server>();

	// MCP Streamable HTTP endpoint
	_httpServer->Post("/mcp", [this](const httplib::Request& req, httplib::Response& res)
	{
		std::string responseBody;
		json request;
		try
		{
			request = json::parse(req.body);
		}
		catch (...)
		{
			json error;
			error["jsonrpc"] = "2.0";
			error["error"]["code"] = -32700;
			error["error"]["message"] = "Parse error";
			error["id"] = nullptr;
			res.set_content(error.dump(), "application/json");
			return;
		}

		std::string method = request.value("method", "");
		if (method == "initialize")
		{
			HandleInitialize(req.body, responseBody);
		}
		else if (method == "tools/list")
		{
			HandleToolsList(req.body, responseBody);
		}
		else if (method == "tools/call")
		{
			HandleToolsCall(req.body, responseBody);
		}
		else
		{
			json error;
			error["jsonrpc"] = "2.0";
			error["error"]["code"] = -32601;
			error["error"]["message"] = "Method not found";
			error["id"] = request.value("id", json(nullptr));
			responseBody = error.dump();
		}

		res.set_content(responseBody, "application/json");
	});

	_httpServer->listen("127.0.0.1", _port);
}

//----------------------------------------------------------------------------------------------------------------------
void MCPServer::Stop()
{
	if (_httpServer)
	{
		_httpServer->stop();
	}
}

//----------------------------------------------------------------------------------------------------------------------
// MCP protocol handlers
//----------------------------------------------------------------------------------------------------------------------
void MCPServer::HandleInitialize(const std::string& requestBody, std::string& responseBody)
{
	json request = json::parse(requestBody);
	json response;
	response["jsonrpc"] = "2.0";
	response["id"] = request.value("id", json(nullptr));
	response["result"]["protocolVersion"] = "2024-11-05";
	response["result"]["capabilities"]["tools"] = json::object();
	response["result"]["serverInfo"]["name"] = "exodus-mcp";
#ifdef EXODUS_MCP_VERSION
	response["result"]["serverInfo"]["version"] = EXODUS_MCP_VERSION;
#else
	response["result"]["serverInfo"]["version"] = "1.0.0";
#endif
	responseBody = response.dump();
}

//----------------------------------------------------------------------------------------------------------------------
void MCPServer::HandleToolsList(const std::string& requestBody, std::string& responseBody)
{
	json request = json::parse(requestBody);
	json response;
	response["jsonrpc"] = "2.0";
	response["id"] = request.value("id", json(nullptr));

	json tools = json::array();

	// System tools
	{
		json tool;
		tool["name"] = "get_system_status";
		tool["description"] = "Get the current system status (running/stopped, loaded modules)";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"] = json::object();
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "run_system";
		tool["description"] = "Start/resume emulation";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"] = json::object();
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "stop_system";
		tool["description"] = "Pause emulation";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"] = json::object();
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "list_devices";
		tool["description"] = "List all loaded devices in the system";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"] = json::object();
		tools.push_back(tool);
	}

	// CPU tools
	{
		json tool;
		tool["name"] = "read_cpu_registers";
		tool["description"] = "Read the program counter from a processor device";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["device"]["type"] = "string";
		tool["inputSchema"]["properties"]["device"]["description"] = "Processor device instance name from list_devices(), e.g. \"Main 68000\" or \"Z80\"";
		tool["inputSchema"]["required"] = json::array({"device"});
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "read_memory";
		tool["description"] = "Read bytes from a processor's memory address space. Returns hex byte string";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["device"]["type"] = "string";
		tool["inputSchema"]["properties"]["device"]["description"] = "Processor device instance name, e.g. \"Main 68000\" or \"Z80\"";
		tool["inputSchema"]["properties"]["address"]["type"] = "string";
		tool["inputSchema"]["properties"]["address"]["description"] = "Start address as hex string \"$FF0000\" or integer. M68000 range: $000000-$FFFFFF";
		tool["inputSchema"]["properties"]["length"]["type"] = "integer";
		tool["inputSchema"]["properties"]["length"]["description"] = "Number of bytes to read (1-4096)";
		tool["inputSchema"]["required"] = json::array({"device", "address", "length"});
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "write_memory";
		tool["description"] = "Write bytes to a processor's memory address space";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["device"]["type"] = "string";
		tool["inputSchema"]["properties"]["device"]["description"] = "Processor device instance name, e.g. \"Main 68000\" or \"Z80\"";
		tool["inputSchema"]["properties"]["address"]["type"] = "string";
		tool["inputSchema"]["properties"]["address"]["description"] = "Start address as hex string \"$FF0000\" or integer";
		tool["inputSchema"]["properties"]["data"]["type"] = "array";
		tool["inputSchema"]["properties"]["data"]["items"]["type"] = "integer";
		tool["inputSchema"]["properties"]["data"]["description"] = "Array of byte values 0-255, e.g. [0, 16, 255]";
		tool["inputSchema"]["required"] = json::array({"device", "address", "data"});
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "disassemble";
		tool["description"] = "Disassemble CPU instructions starting at an address. Returns plain text with one instruction per line";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["device"]["type"] = "string";
		tool["inputSchema"]["properties"]["device"]["description"] = "Processor device instance name, e.g. \"Main 68000\" or \"Z80\"";
		tool["inputSchema"]["properties"]["address"]["type"] = "string";
		tool["inputSchema"]["properties"]["address"]["description"] = "Start address as hex string \"$000200\" or integer";
		tool["inputSchema"]["properties"]["count"]["type"] = "integer";
		tool["inputSchema"]["properties"]["count"]["description"] = "Number of instructions to disassemble (optional, default 10)";
		tool["inputSchema"]["required"] = json::array({"device", "address"});
		tools.push_back(tool);
	}

	// Memory search
	{
		json tool;
		tool["name"] = "search_memory";
		tool["description"] = "Search a processor's memory space for a hex byte pattern. Returns list of matching addresses";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["device"]["type"] = "string";
		tool["inputSchema"]["properties"]["device"]["description"] = "Processor device instance name, e.g. \"Main 68000\"";
		tool["inputSchema"]["properties"]["hex"]["type"] = "string";
		tool["inputSchema"]["properties"]["hex"]["description"] = "Hex byte pattern to search for, e.g. \"00FF8000\" for bytes 00 FF 80 00. No spaces or $ prefix";
		tool["inputSchema"]["properties"]["start"]["type"] = "string";
		tool["inputSchema"]["properties"]["start"]["description"] = "Start address (optional, default \"$000000\")";
		tool["inputSchema"]["properties"]["end"]["type"] = "string";
		tool["inputSchema"]["properties"]["end"]["description"] = "End address (optional, default \"$FFFFFF\" for M68000)";
		tool["inputSchema"]["required"] = json::array({"device", "hex"});
		tools.push_back(tool);
	}

	// Breakpoint tools
	{
		json tool;
		tool["name"] = "set_breakpoint";
		tool["description"] = "Set an execution breakpoint at an address on a processor device";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["device"]["type"] = "string";
		tool["inputSchema"]["properties"]["device"]["description"] = "Processor device instance name, e.g. \"Main 68000\" or \"Z80\"";
		tool["inputSchema"]["properties"]["address"]["type"] = "string";
		tool["inputSchema"]["properties"]["address"]["description"] = "Breakpoint address as hex string \"$000200\" or integer";
		tool["inputSchema"]["required"] = json::array({"device", "address"});
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "remove_breakpoint";
		tool["description"] = "Remove an execution breakpoint at an address";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["device"]["type"] = "string";
		tool["inputSchema"]["properties"]["device"]["description"] = "Processor device instance name, e.g. \"Main 68000\" or \"Z80\"";
		tool["inputSchema"]["properties"]["address"]["type"] = "string";
		tool["inputSchema"]["properties"]["address"]["description"] = "Address of breakpoint to remove, as hex string \"$000200\" or integer";
		tool["inputSchema"]["required"] = json::array({"device", "address"});
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "list_breakpoints";
		tool["description"] = "List all active breakpoints on a processor device";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["device"]["type"] = "string";
		tool["inputSchema"]["properties"]["device"]["description"] = "Processor device instance name, e.g. \"Main 68000\" or \"Z80\"";
		tool["inputSchema"]["required"] = json::array({"device"});
		tools.push_back(tool);
	}

	// Watchpoint tools
	{
		json tool;
		tool["name"] = "set_watchpoint";
		tool["description"] = "Set a memory watchpoint that breaks when an address range is read from or written to";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["device"]["type"] = "string";
		tool["inputSchema"]["properties"]["device"]["description"] = "Processor device instance name, e.g. \"Main 68000\"";
		tool["inputSchema"]["properties"]["address"]["type"] = "string";
		tool["inputSchema"]["properties"]["address"]["description"] = "Start address as hex string \"$FF8E00\" or integer";
		tool["inputSchema"]["properties"]["size"]["type"] = "integer";
		tool["inputSchema"]["properties"]["size"]["description"] = "Size of memory range in bytes (optional, default 1)";
		tool["inputSchema"]["properties"]["read"]["type"] = "boolean";
		tool["inputSchema"]["properties"]["read"]["description"] = "Break on reads (optional, default false)";
		tool["inputSchema"]["properties"]["write"]["type"] = "boolean";
		tool["inputSchema"]["properties"]["write"]["description"] = "Break on writes (optional, default true)";
		tool["inputSchema"]["required"] = json::array({"device", "address"});
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "remove_watchpoint";
		tool["description"] = "Remove a memory watchpoint at an address";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["device"]["type"] = "string";
		tool["inputSchema"]["properties"]["device"]["description"] = "Processor device instance name, e.g. \"Main 68000\"";
		tool["inputSchema"]["properties"]["address"]["type"] = "string";
		tool["inputSchema"]["properties"]["address"]["description"] = "Address of watchpoint to remove, as hex string \"$FF8E00\" or integer";
		tool["inputSchema"]["required"] = json::array({"device", "address"});
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "list_watchpoints";
		tool["description"] = "List all active memory watchpoints on a processor device";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["device"]["type"] = "string";
		tool["inputSchema"]["properties"]["device"]["description"] = "Processor device instance name, e.g. \"Main 68000\"";
		tool["inputSchema"]["required"] = json::array({"device"});
		tools.push_back(tool);
	}

	// Execution tools
	{
		json tool;
		tool["name"] = "step_device";
		tool["description"] = "Execute a single instruction on a processor device. System must be stopped first";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["device"]["type"] = "string";
		tool["inputSchema"]["properties"]["device"]["description"] = "Processor device instance name, e.g. \"Main 68000\" or \"Z80\"";
		tool["inputSchema"]["required"] = json::array({"device"});
		tools.push_back(tool);
	}

	// VDP raw memory tools
	{
		json tool;
		tool["name"] = "read_vram";
		tool["description"] = "Read raw bytes from VDP VRAM (65536 bytes). Contains tile patterns, nametables, sprite table, H-scroll table. No device parameter needed";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["address"]["type"] = "string";
		tool["inputSchema"]["properties"]["address"]["description"] = "VRAM address as hex string \"$0000\" or integer. Range: $0000-$FFFF";
		tool["inputSchema"]["properties"]["length"]["type"] = "integer";
		tool["inputSchema"]["properties"]["length"]["description"] = "Number of bytes to read (1-4096)";
		tool["inputSchema"]["required"] = json::array({"address", "length"});
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "read_cram";
		tool["description"] = "Read raw bytes from VDP CRAM (128 bytes). Stores 64 colors as 9-bit BGR, 2 bytes each. No device parameter needed";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["address"]["type"] = "string";
		tool["inputSchema"]["properties"]["address"]["description"] = "CRAM address as hex string \"$00\" or integer. Range: $00-$7F";
		tool["inputSchema"]["properties"]["length"]["type"] = "integer";
		tool["inputSchema"]["properties"]["length"]["description"] = "Number of bytes to read (1-128)";
		tool["inputSchema"]["required"] = json::array({"address", "length"});
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "read_vsram";
		tool["description"] = "Read raw bytes from VDP VSRAM (80 bytes). Stores per-column vertical scroll values, 2 bytes per column. No device parameter needed";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["address"]["type"] = "string";
		tool["inputSchema"]["properties"]["address"]["description"] = "VSRAM address as hex string \"$00\" or integer. Range: $00-$4F";
		tool["inputSchema"]["properties"]["length"]["type"] = "integer";
		tool["inputSchema"]["properties"]["length"]["description"] = "Number of bytes to read (1-80)";
		tool["inputSchema"]["required"] = json::array({"address", "length"});
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "read_vdp_registers";
		tool["description"] = "Read all 24 VDP registers as plain text (R00-R23 with $XX hex values). No device parameter needed";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"] = json::object();
		tools.push_back(tool);
	}

	// VDP decoded data tools
	{
		json tool;
		tool["name"] = "read_sprite_table";
		tool["description"] = "Decode the full sprite attribute table (up to 80 sprites). Returns plain text table with columns: index, x, y, width, height, pattern, palette, priority, hflip, vflip, link. Stops at end of sprite link chain. No device parameter needed";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"] = json::object();
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "read_palette";
		tool["description"] = "Read all 64 VDP colors (4 rows x 16 colors) decoded to 8-bit RGB values and hex color codes. No device parameter needed";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"] = json::object();
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "read_nametable";
		tool["description"] = "Decode a plane's nametable. Returns compact one-row-per-line format with tile index, palette, and flags per cell. Use row_start/row_count to page through large planes. No device parameter needed";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["plane"]["type"] = "string";
		tool["inputSchema"]["properties"]["plane"]["description"] = "Which plane to read. Must be one of: \"a\" (Plane A/Scroll A), \"b\" (Plane B/Scroll B), or \"window\" (Window plane)";
		tool["inputSchema"]["properties"]["plane"]["enum"] = json::array({"a", "b", "window"});
		tool["inputSchema"]["properties"]["row_start"]["type"] = "integer";
		tool["inputSchema"]["properties"]["row_start"]["description"] = "First row to read (optional, default 0)";
		tool["inputSchema"]["properties"]["row_count"]["type"] = "integer";
		tool["inputSchema"]["properties"]["row_count"]["description"] = "Number of rows to read (optional, default all rows)";
		tool["inputSchema"]["required"] = json::array({"plane"});
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "read_vdp_state";
		tool["description"] = "Read the full decoded VDP configuration: display on/off, mode, resolution, plane size in cells and pixels, nametable base addresses (A/B/window/sprite/hscroll), scroll modes, background color, auto-increment, DMA status. No device parameter needed";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"] = json::object();
		tools.push_back(tool);
	}

	// Screenshot & pixel info tools
	{
		json tool;
		tool["name"] = "screenshot";
		tool["description"] = "Capture the current VDP rendered frame as a base64-encoded PNG image. No device parameter needed";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"] = json::object();
		tools.push_back(tool);
	}
	{
		json tool;
		tool["name"] = "query_pixel";
		tool["description"] = "Get detailed rendering info for a screen pixel: which layer produced it (Sprite/LayerA/LayerB/Window/Background), tile mapping VRAM address, pattern row/column, palette, color, and sprite entry details. Coordinates are relative to the rendered frame (0,0 = top-left). No device parameter needed";
		tool["inputSchema"]["type"] = "object";
		tool["inputSchema"]["properties"]["x"]["type"] = "integer";
		tool["inputSchema"]["properties"]["x"]["description"] = "Horizontal pixel position (0 = left edge). H32 mode: 0-255, H40 mode: 0-319";
		tool["inputSchema"]["properties"]["y"]["type"] = "integer";
		tool["inputSchema"]["properties"]["y"]["description"] = "Vertical pixel position (0 = top edge). Typical range: 0-223 (V28) or 0-239 (V30)";
		tool["inputSchema"]["required"] = json::array({"x", "y"});
		tools.push_back(tool);
	}

	response["result"]["tools"] = tools;
	responseBody = response.dump();
}

//----------------------------------------------------------------------------------------------------------------------
void MCPServer::HandleToolsCall(const std::string& requestBody, std::string& responseBody)
{
	json request = json::parse(requestBody);
	json response;
	response["jsonrpc"] = "2.0";
	response["id"] = request.value("id", json(nullptr));

	std::string toolName = request["params"]["name"].get<std::string>();
	json args = request["params"].value("arguments", json::object());

	// Helper: extract address from JSON value (accepts string "$FF0000", "0xFF0000", "FF0000h" or integer)
	auto getAddr = [](const json& val) -> unsigned int {
		if (val.is_string())
			return MCPServer::ParseAddress(val.get<std::string>());
		return val.get<unsigned int>();
	};

	std::string resultText;

	try
	{
		if (toolName == "get_system_status")
			resultText = ToolGetSystemStatus();
		else if (toolName == "run_system")
			resultText = ToolRunSystem();
		else if (toolName == "stop_system")
			resultText = ToolStopSystem();
		else if (toolName == "list_devices")
			resultText = ToolListDevices();
		else if (toolName == "read_cpu_registers")
			resultText = ToolReadCPURegisters(args["device"].get<std::string>());
		else if (toolName == "read_memory")
			resultText = ToolReadMemory(args["device"].get<std::string>(), getAddr(args["address"]), args["length"].get<unsigned int>());
		else if (toolName == "write_memory")
		{
			std::vector<unsigned char> data;
			for (auto& b : args["data"])
				data.push_back(static_cast<unsigned char>(b.get<int>()));
			resultText = ToolWriteMemory(args["device"].get<std::string>(), getAddr(args["address"]), data);
		}
		else if (toolName == "disassemble")
			resultText = ToolDisassemble(args["device"].get<std::string>(), getAddr(args["address"]), args.value("count", 10));
		else if (toolName == "search_memory")
		{
			// Parse hex pattern string into bytes
			std::string hexStr = args["hex"].get<std::string>();
			std::vector<unsigned char> pattern;
			for (size_t i = 0; i + 1 < hexStr.size(); i += 2)
			{
				pattern.push_back((unsigned char)strtoul(hexStr.substr(i, 2).c_str(), nullptr, 16));
			}
			unsigned int startAddr = args.contains("start") ? getAddr(args["start"]) : 0;
			unsigned int endAddr = args.contains("end") ? getAddr(args["end"]) : 0xFFFFFF;
			resultText = ToolSearchMemory(args["device"].get<std::string>(), startAddr, endAddr, pattern);
		}
		else if (toolName == "set_breakpoint")
			resultText = ToolSetBreakpoint(args["device"].get<std::string>(), getAddr(args["address"]));
		else if (toolName == "remove_breakpoint")
			resultText = ToolRemoveBreakpoint(args["device"].get<std::string>(), getAddr(args["address"]));
		else if (toolName == "list_breakpoints")
			resultText = ToolListBreakpoints(args["device"].get<std::string>());
		else if (toolName == "set_watchpoint")
			resultText = ToolSetWatchpoint(args["device"].get<std::string>(), getAddr(args["address"]), args.value("size", 1), args.value("read", false), args.value("write", true));
		else if (toolName == "remove_watchpoint")
			resultText = ToolRemoveWatchpoint(args["device"].get<std::string>(), getAddr(args["address"]));
		else if (toolName == "list_watchpoints")
			resultText = ToolListWatchpoints(args["device"].get<std::string>());
		else if (toolName == "step_device")
			resultText = ToolStepDevice(args["device"].get<std::string>());
		else if (toolName == "read_vram")
		{
			if (!args.contains("address"))
				throw std::runtime_error("Missing required parameter: address. This tool reads VDP VRAM directly (no device parameter needed)");
			if (!args.contains("length"))
				throw std::runtime_error("Missing required parameter: length. This tool reads VDP VRAM directly (no device parameter needed)");
			resultText = ToolReadVRAM(getAddr(args["address"]), args["length"].get<unsigned int>());
		}
		else if (toolName == "read_cram")
		{
			if (!args.contains("address"))
				throw std::runtime_error("Missing required parameter: address. This tool reads VDP CRAM directly (no device parameter needed)");
			if (!args.contains("length"))
				throw std::runtime_error("Missing required parameter: length. This tool reads VDP CRAM directly (no device parameter needed)");
			resultText = ToolReadCRAM(getAddr(args["address"]), args["length"].get<unsigned int>());
		}
		else if (toolName == "read_vsram")
		{
			if (!args.contains("address"))
				throw std::runtime_error("Missing required parameter: address. This tool reads VDP VSRAM directly (no device parameter needed)");
			if (!args.contains("length"))
				throw std::runtime_error("Missing required parameter: length. This tool reads VDP VSRAM directly (no device parameter needed)");
			resultText = ToolReadVSRAM(getAddr(args["address"]), args["length"].get<unsigned int>());
		}
		else if (toolName == "read_vdp_registers")
			resultText = ToolReadVDPRegisters();
		else if (toolName == "read_sprite_table")
			resultText = ToolReadSpriteTable();
		else if (toolName == "read_palette")
			resultText = ToolReadPalette();
		else if (toolName == "read_nametable")
		{
			if (!args.contains("plane") || !args["plane"].is_string())
				throw std::runtime_error("Missing required parameter: plane (must be \"a\", \"b\", or \"window\"). This tool reads VDP nametables directly (no device parameter needed)");
			resultText = ToolReadNametable(args["plane"].get<std::string>(), args.value("row_start", 0), args.value("row_count", 0));
		}
		else if (toolName == "read_vdp_state")
			resultText = ToolReadVDPState();
		else if (toolName == "screenshot")
			resultText = ToolScreenshot();
		else if (toolName == "query_pixel")
		{
			if (!args.contains("x") || !args.contains("y"))
				throw std::runtime_error("Missing required parameters: x and y. This tool queries pixel info from the VDP rendered frame (no device parameter needed)");
			resultText = ToolQueryPixel(args["x"].get<unsigned int>(), args["y"].get<unsigned int>());
		}
		else
		{
			response["error"]["code"] = -32602;
			response["error"]["message"] = "Unknown tool: " + toolName;
			responseBody = response.dump();
			return;
		}
	}
	catch (const std::exception& ex)
	{
		response["result"]["content"] = json::array();
		json contentItem;
		contentItem["type"] = "text";
		contentItem["text"] = std::string("Error: ") + ex.what();
		response["result"]["content"].push_back(contentItem);
		response["result"]["isError"] = true;
		responseBody = response.dump();
		return;
	}

	response["result"]["content"] = json::array();
	json contentItem;
	contentItem["type"] = "text";
	contentItem["text"] = resultText;
	response["result"]["content"].push_back(contentItem);
	responseBody = response.dump();
}

//----------------------------------------------------------------------------------------------------------------------
// Tool implementations - System
//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolGetSystemStatus()
{
	json result;
	result["running"] = _systemInterface.SystemRunning();

	json modules = json::array();
	std::list<unsigned int> moduleIDs = _systemInterface.GetLoadedModuleIDs();
	for (unsigned int id : moduleIDs)
	{
		json mod;
		mod["id"] = id;
		std::wstring displayName;
		_systemInterface.GetModuleDisplayName(id, displayName);
		std::string name(displayName.begin(), displayName.end());
		mod["name"] = name;
		modules.push_back(mod);
	}
	result["modules"] = modules;

	return result.dump(2);
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolRunSystem()
{
	_systemInterface.RunSystem();
	return "{\"status\": \"running\"}";
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolStopSystem()
{
	_systemInterface.StopSystem();
	return "{\"status\": \"stopped\"}";
}

//----------------------------------------------------------------------------------------------------------------------
// Tool implementations - Devices
//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolListDevices()
{
	json result = json::array();
	std::list<IDevice*> devices = _systemInterface.GetLoadedDevices();
	for (IDevice* device : devices)
	{
		json dev;
		std::wstring className = device->GetDeviceClassName();
		std::wstring instanceName = device->GetDeviceInstanceName();
		dev["class"] = std::string(className.begin(), className.end());
		dev["instance"] = std::string(instanceName.begin(), instanceName.end());
		result.push_back(dev);
	}
	return result.dump(2);
}

//----------------------------------------------------------------------------------------------------------------------
// Tool implementations - CPU
//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolReadCPURegisters(const std::string& deviceName)
{
	IDevice* device = FindDeviceByName(deviceName);
	if (!device)
		throw std::runtime_error("Device not found: " + deviceName);

	IProcessor* processor = dynamic_cast<IProcessor*>(device);
	if (!processor)
		throw std::runtime_error("Device is not a processor: " + deviceName);

	// Try M68000-specific interface for full registers
	IM68000* m68k = dynamic_cast<IM68000*>(device);
	if (m68k)
	{
		std::string result;
		char line[64];
		for (int i = 0; i < 8; ++i)
		{
			snprintf(line, sizeof(line), "D%d=$%08X  A%d=$%08X\n", i, m68k->GetD(i), i, m68k->GetA(i));
			result += line;
		}
		snprintf(line, sizeof(line), "PC=$%06X  SR=$%04X  SSP=$%08X  USP=$%08X\n",
			m68k->GetPC(), m68k->GetSR(), m68k->GetSSP(), m68k->GetUSP());
		result += line;
		return result;
	}

	// Generic fallback - just PC
	char pcStr[64];
	snprintf(pcStr, sizeof(pcStr), "PC=$%06X", processor->GetCurrentPC());
	return std::string(pcStr);
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolReadMemory(const std::string& deviceName, unsigned int address, unsigned int length)
{
	IDevice* device = FindDeviceByName(deviceName);
	if (!device)
		throw std::runtime_error("Device not found: " + deviceName);

	IProcessor* processor = dynamic_cast<IProcessor*>(device);
	if (!processor)
		throw std::runtime_error("Device is not a processor: " + deviceName);

	if (length > 4096)
		length = 4096;

	char addrStr[16];
	snprintf(addrStr, sizeof(addrStr), "$%06X", address);

	json result;
	result["address"] = addrStr;
	result["length"] = length;

	std::string hexStr;
	hexStr.reserve(length * 3);
	char buf[4];
	for (unsigned int i = 0; i < length; ++i)
	{
		unsigned int byteVal = processor->GetMemorySpaceByte(address + i);
		snprintf(buf, sizeof(buf), "%02X ", byteVal);
		hexStr += buf;
	}
	result["hex"] = hexStr;

	return result.dump(2);
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolWriteMemory(const std::string& deviceName, unsigned int address, const std::vector<unsigned char>& data)
{
	IDevice* device = FindDeviceByName(deviceName);
	if (!device)
		throw std::runtime_error("Device not found: " + deviceName);

	IProcessor* processor = dynamic_cast<IProcessor*>(device);
	if (!processor)
		throw std::runtime_error("Device is not a processor: " + deviceName);

	for (size_t i = 0; i < data.size(); ++i)
	{
		processor->SetMemorySpaceByte(address + (unsigned int)i, data[i]);
	}

	char addrStr[16];
	snprintf(addrStr, sizeof(addrStr), "$%06X", address);
	json result;
	result["address"] = addrStr;
	result["bytesWritten"] = data.size();
	return result.dump(2);
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolDisassemble(const std::string& deviceName, unsigned int address, unsigned int count)
{
	IDevice* device = FindDeviceByName(deviceName);
	if (!device)
		throw std::runtime_error("Device not found: " + deviceName);

	IProcessor* processor = dynamic_cast<IProcessor*>(device);
	if (!processor)
		throw std::runtime_error("Device is not a processor: " + deviceName);

	std::string result;
	unsigned int currentAddress = address;
	unsigned int instructionsEmitted = 0;
	while (instructionsEmitted < count)
	{
		OpcodeInfo opcodeInfo;
		if (!processor->GetOpcodeInfo(currentAddress, opcodeInfo))
			break;

		if (!opcodeInfo.GetIsValidOpcode() || opcodeInfo.GetOpcodeSize() == 0)
		{
			// Skip non-code region, count how many bytes we skip
			unsigned int skipStart = currentAddress;
			while (instructionsEmitted < count)
			{
				currentAddress += 2;
				OpcodeInfo nextInfo;
				if (!processor->GetOpcodeInfo(currentAddress, nextInfo))
					break;
				if (nextInfo.GetIsValidOpcode() && nextInfo.GetOpcodeSize() > 0)
					break;
			}
			char line[128];
			snprintf(line, sizeof(line), "         ; $%06X-$%06X: %u bytes skipped (not code)\n",
				skipStart, currentAddress - 1, currentAddress - skipStart);
			result += line;
			continue;
		}

		std::wstring wdisasm = opcodeInfo.GetOpcodeNameDisassembly();
		std::wstring wargs = opcodeInfo.GetOpcodeArgumentsDisassembly();
		std::string mnemonic(wdisasm.begin(), wdisasm.end());
		std::string operands(wargs.begin(), wargs.end());

		char line[256];
		snprintf(line, sizeof(line), "$%06X  %-9s %s\n", currentAddress, mnemonic.c_str(), operands.c_str());
		result += line;

		currentAddress += opcodeInfo.GetOpcodeSize();
		++instructionsEmitted;
	}

	return result;
}

//----------------------------------------------------------------------------------------------------------------------
// Tool implementations - Memory search
//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolSearchMemory(const std::string& deviceName, unsigned int startAddress, unsigned int endAddress, const std::vector<unsigned char>& pattern)
{
	IDevice* device = FindDeviceByName(deviceName);
	if (!device)
		throw std::runtime_error("Device not found: " + deviceName);

	IProcessor* processor = dynamic_cast<IProcessor*>(device);
	if (!processor)
		throw std::runtime_error("Device is not a processor: " + deviceName);

	if (pattern.empty())
		throw std::runtime_error("Search pattern is empty");

	// Cap results to avoid huge responses
	const unsigned int maxResults = 64;
	std::string result;
	char line[32];
	unsigned int found = 0;

	for (unsigned int addr = startAddress; addr <= endAddress - (unsigned int)pattern.size() + 1; ++addr)
	{
		bool match = true;
		for (size_t i = 0; i < pattern.size(); ++i)
		{
			if (processor->GetMemorySpaceByte(addr + (unsigned int)i) != pattern[i])
			{
				match = false;
				break;
			}
		}
		if (match)
		{
			snprintf(line, sizeof(line), "$%06X\n", addr);
			result += line;
			++found;
			if (found >= maxResults)
			{
				result += "(max 64 results reached)\n";
				break;
			}
		}
	}

	if (found == 0)
		return "No matches found";

	char header[64];
	snprintf(header, sizeof(header), "%u match(es):\n", found);
	return std::string(header) + result;
}

//----------------------------------------------------------------------------------------------------------------------
// Tool implementations - Breakpoints
//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolSetBreakpoint(const std::string& deviceName, unsigned int address)
{
	IDevice* device = FindDeviceByName(deviceName);
	if (!device)
		throw std::runtime_error("Device not found: " + deviceName);

	IProcessor* processor = dynamic_cast<IProcessor*>(device);
	if (!processor)
		throw std::runtime_error("Device is not a processor: " + deviceName);

	IBreakpoint* bp = processor->CreateBreakpoint();
	if (processor->LockBreakpoint(bp))
	{
		bp->SetLocationConditionData1(address);
		bp->SetName(bp->GenerateName());
		bp->SetEnabled(true);
		processor->UnlockBreakpoint(bp);
	}

	char addrStr[16];
	snprintf(addrStr, sizeof(addrStr), "$%06X", address);
	json result;
	result["address"] = addrStr;
	result["status"] = "set";
	return result.dump(2);
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolRemoveBreakpoint(const std::string& deviceName, unsigned int address)
{
	IDevice* device = FindDeviceByName(deviceName);
	if (!device)
		throw std::runtime_error("Device not found: " + deviceName);

	IProcessor* processor = dynamic_cast<IProcessor*>(device);
	if (!processor)
		throw std::runtime_error("Device is not a processor: " + deviceName);

	std::list<IBreakpoint*> breakpoints = processor->GetBreakpointList();
	for (IBreakpoint* bp : breakpoints)
	{
		if (bp->GetLocationConditionData1() == address)
		{
			processor->DeleteBreakpoint(bp);
			char addrStr[16];
			snprintf(addrStr, sizeof(addrStr), "$%06X", address);
			json result;
			result["address"] = addrStr;
			result["status"] = "removed";
			return result.dump(2);
		}
	}

	throw std::runtime_error("No breakpoint found at address");
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolListBreakpoints(const std::string& deviceName)
{
	IDevice* device = FindDeviceByName(deviceName);
	if (!device)
		throw std::runtime_error("Device not found: " + deviceName);

	IProcessor* processor = dynamic_cast<IProcessor*>(device);
	if (!processor)
		throw std::runtime_error("Device is not a processor: " + deviceName);

	json result = json::array();
	std::list<IBreakpoint*> breakpoints = processor->GetBreakpointList();
	for (IBreakpoint* bp : breakpoints)
	{
		json bpInfo;
		char addrStr[16];
		snprintf(addrStr, sizeof(addrStr), "$%06X", bp->GetLocationConditionData1());
		bpInfo["address"] = addrStr;
		bpInfo["enabled"] = bp->GetEnabled();
		result.push_back(bpInfo);
	}
	return result.dump(2);
}

//----------------------------------------------------------------------------------------------------------------------
// Tool implementations - Watchpoints
//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolSetWatchpoint(const std::string& deviceName, unsigned int address, unsigned int size, bool onRead, bool onWrite)
{
	IDevice* device = FindDeviceByName(deviceName);
	if (!device)
		throw std::runtime_error("Device not found: " + deviceName);

	IProcessor* processor = dynamic_cast<IProcessor*>(device);
	if (!processor)
		throw std::runtime_error("Device is not a processor: " + deviceName);

	IWatchpoint* wp = processor->CreateWatchpoint();
	if (processor->LockWatchpoint(wp))
	{
		wp->SetLocationConditionData1(address);
		wp->SetLocationConditionData2(address + size - 1);
		wp->SetLocationCondition(size > 1 ? IWatchpoint::Condition::GreaterAndLess : IWatchpoint::Condition::Equal);
		wp->SetOnRead(onRead);
		wp->SetOnWrite(onWrite);
		wp->SetBreakEvent(true);
		wp->SetEnabled(true);
		wp->SetName(wp->GenerateName());
		processor->UnlockWatchpoint(wp);
	}

	char addrStr[16];
	snprintf(addrStr, sizeof(addrStr), "$%06X", address);
	std::string result = "Watchpoint set at " + std::string(addrStr);
	char sizeStr[32];
	snprintf(sizeStr, sizeof(sizeStr), " (%u bytes", size);
	result += sizeStr;
	if (onRead && onWrite) result += ", read+write)";
	else if (onWrite) result += ", write)";
	else if (onRead) result += ", read)";
	else result += ")";
	return result;
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolRemoveWatchpoint(const std::string& deviceName, unsigned int address)
{
	IDevice* device = FindDeviceByName(deviceName);
	if (!device)
		throw std::runtime_error("Device not found: " + deviceName);

	IProcessor* processor = dynamic_cast<IProcessor*>(device);
	if (!processor)
		throw std::runtime_error("Device is not a processor: " + deviceName);

	std::list<IWatchpoint*> watchpoints = processor->GetWatchpointList();
	for (IWatchpoint* wp : watchpoints)
	{
		if (wp->GetLocationConditionData1() == address)
		{
			processor->DeleteWatchpoint(wp);
			char addrStr[16];
			snprintf(addrStr, sizeof(addrStr), "$%06X", address);
			return "Watchpoint removed at " + std::string(addrStr);
		}
	}

	throw std::runtime_error("No watchpoint found at address");
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolListWatchpoints(const std::string& deviceName)
{
	IDevice* device = FindDeviceByName(deviceName);
	if (!device)
		throw std::runtime_error("Device not found: " + deviceName);

	IProcessor* processor = dynamic_cast<IProcessor*>(device);
	if (!processor)
		throw std::runtime_error("Device is not a processor: " + deviceName);

	std::list<IWatchpoint*> watchpoints = processor->GetWatchpointList();
	if (watchpoints.empty())
		return "No watchpoints set";

	std::string result;
	char line[128];
	for (IWatchpoint* wp : watchpoints)
	{
		const char* type = (wp->GetOnRead() && wp->GetOnWrite()) ? "R/W" :
		                   wp->GetOnWrite() ? "W" :
		                   wp->GetOnRead() ? "R" : "?";
		snprintf(line, sizeof(line), "$%06X-$%06X %s %s\n",
			wp->GetLocationConditionData1(), wp->GetLocationConditionData2(),
			type, wp->GetEnabled() ? "enabled" : "disabled");
		result += line;
	}
	return result;
}

//----------------------------------------------------------------------------------------------------------------------
// Tool implementations - VDP
//----------------------------------------------------------------------------------------------------------------------
static IS315_5313* FindVDP(ISystemExtensionInterface& systemInterface)
{
	std::list<IDevice*> devices = systemInterface.GetLoadedDevices();
	for (IDevice* device : devices)
	{
		IS315_5313* vdp = dynamic_cast<IS315_5313*>(device);
		if (vdp)
			return vdp;
	}
	return nullptr;
}

//----------------------------------------------------------------------------------------------------------------------
static std::string ReadTimedBuffer(ITimedBufferInt* buffer, unsigned int address, unsigned int length, unsigned int maxSize)
{
	if (address >= maxSize)
		throw std::runtime_error("Address out of range");
	if (address + length > maxSize)
		length = maxSize - address;

	char addrStr[16];
	snprintf(addrStr, sizeof(addrStr), "$%04X", address);

	std::string hexStr;
	hexStr.reserve(length * 3);
	char buf[4];
	for (unsigned int i = 0; i < length; ++i)
	{
		unsigned char byteVal = buffer->ReadLatest(address + i);
		snprintf(buf, sizeof(buf), "%02X ", byteVal);
		hexStr += buf;
	}

	json result;
	result["address"] = addrStr;
	result["length"] = length;
	result["hex"] = hexStr;
	return result.dump(2);
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolReadVRAM(unsigned int address, unsigned int length)
{
	IS315_5313* vdp = FindVDP(_systemInterface);
	if (!vdp)
		throw std::runtime_error("VDP device not found");
	return ReadTimedBuffer(vdp->GetVRAMBuffer(), address, length, 0x10000);
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolReadCRAM(unsigned int address, unsigned int length)
{
	IS315_5313* vdp = FindVDP(_systemInterface);
	if (!vdp)
		throw std::runtime_error("VDP device not found");
	return ReadTimedBuffer(vdp->GetCRAMBuffer(), address, length, 0x80);
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolReadVSRAM(unsigned int address, unsigned int length)
{
	IS315_5313* vdp = FindVDP(_systemInterface);
	if (!vdp)
		throw std::runtime_error("VDP device not found");
	return ReadTimedBuffer(vdp->GetVSRAMBuffer(), address, length, 0x50);
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolReadVDPRegisters()
{
	IS315_5313* vdp = FindVDP(_systemInterface);
	if (!vdp)
		throw std::runtime_error("VDP device not found");

	std::string result;
	char line[64];
	for (unsigned int i = 0; i < IS315_5313::RegisterCount; ++i)
	{
		unsigned int val = vdp->GetRegisterData(i);
		snprintf(line, sizeof(line), "R%02u = $%02X\n", i, val);
		result += line;
	}
	return result;
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolReadSpriteTable()
{
	IS315_5313* vdp = FindVDP(_systemInterface);
	if (!vdp)
		throw std::runtime_error("VDP device not found");

	unsigned int spriteTableBase = vdp->RegGetNameTableBaseSprite();

	std::string result;
	result += "#    X      Y     W  H  Pat     Pal  Pri HF VF Link\n";
	char line[128];
	for (unsigned int i = 0; i < 80; ++i)
	{
		IS315_5313::SpriteMappingTableEntry s = vdp->GetSpriteMappingTableEntry(spriteTableBase, i);
		snprintf(line, sizeof(line), "%-4u %-6u %-5u %ux%u $%03X   %u    %u   %u  %u  %u\n",
			i, s.xpos, s.ypos, s.width, s.height, s.blockNumber,
			s.paletteLine, s.priority ? 1 : 0, s.hflip ? 1 : 0, s.vflip ? 1 : 0, s.link);
		result += line;

		// Stop at end of sprite list (link = 0 terminates, except for sprite 0)
		if (i > 0 && s.link == 0)
			break;
	}
	return result;
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolReadPalette()
{
	IS315_5313* vdp = FindVDP(_systemInterface);
	if (!vdp)
		throw std::runtime_error("VDP device not found");

	std::string result;
	char line[64];
	for (unsigned int row = 0; row < 4; ++row)
	{
		snprintf(line, sizeof(line), "Palette %u:\n", row);
		result += line;
		for (unsigned int col = 0; col < 16; ++col)
		{
			IS315_5313::DecodedPaletteColorEntry color = vdp->ReadDecodedPaletteColor(row, col);
			unsigned char r = vdp->ColorValueTo8BitValue(color.r, false, false);
			unsigned char g = vdp->ColorValueTo8BitValue(color.g, false, false);
			unsigned char b = vdp->ColorValueTo8BitValue(color.b, false, false);
			snprintf(line, sizeof(line), "  %2u: R=%3u G=%3u B=%3u (#%02X%02X%02X)\n", col, r, g, b, r, g, b);
			result += line;
		}
	}
	return result;
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolReadNametable(const std::string& plane, unsigned int rowStart, unsigned int rowCount)
{
	IS315_5313* vdp = FindVDP(_systemInterface);
	if (!vdp)
		throw std::runtime_error("VDP device not found");

	// Get the nametable base address for the requested plane
	unsigned int nameTableBase;
	std::string planeName;
	if (plane == "a" || plane == "A" || plane == "scroll_a" || plane == "Plane A")
	{
		nameTableBase = vdp->RegGetNameTableBaseScrollA();
		planeName = "Plane A";
	}
	else if (plane == "b" || plane == "B" || plane == "scroll_b" || plane == "Plane B")
	{
		nameTableBase = vdp->RegGetNameTableBaseScrollB();
		planeName = "Plane B";
	}
	else if (plane == "w" || plane == "W" || plane == "window" || plane == "Window")
	{
		nameTableBase = vdp->RegGetNameTableBaseWindow();
		planeName = "Window";
	}
	else
	{
		throw std::runtime_error("Unknown plane: " + plane + ". Use \"a\", \"b\", or \"window\"");
	}

	// Determine plane dimensions from registers
	unsigned int hsz = vdp->GetRegisterData(0x10) & 0x03;
	unsigned int vsz = (vdp->GetRegisterData(0x10) >> 4) & 0x03;
	unsigned int planeWidthCells = (hsz == 0) ? 32 : (hsz == 1) ? 64 : (hsz == 3) ? 128 : 32;
	unsigned int planeHeightCells = (vsz == 0) ? 32 : (vsz == 1) ? 64 : (vsz == 3) ? 128 : 32;

	// Clamp row range (default to 8 rows to keep output manageable)
	if (rowStart >= planeHeightCells)
		rowStart = 0;
	if (rowCount == 0)
		rowCount = 8;
	if (rowStart + rowCount > planeHeightCells)
		rowCount = planeHeightCells - rowStart;

	ITimedBufferInt* vram = vdp->GetVRAMBuffer();

	char header[128];
	snprintf(header, sizeof(header), "%s: %ux%u cells, base=$%04X, rows %u-%u\n",
		planeName.c_str(), planeWidthCells, planeHeightCells, nameTableBase,
		rowStart, rowStart + rowCount - 1);
	std::string result = header;

	result += "Row Col Pat     Pal Pri HF VF\n";

	char line[80];
	for (unsigned int row = rowStart; row < rowStart + rowCount; ++row)
	{
		for (unsigned int col = 0; col < planeWidthCells; ++col)
		{
			unsigned int entryAddr = nameTableBase + (row * planeWidthCells + col) * 2;
			unsigned int hi = vram->ReadLatest(entryAddr);
			unsigned int lo = vram->ReadLatest(entryAddr + 1);
			unsigned int entry = (hi << 8) | lo;

			unsigned int pattern = entry & 0x7FF;
			bool hflip = (entry >> 11) & 1;
			bool vflip = (entry >> 12) & 1;
			unsigned int palette = (entry >> 13) & 3;
			bool priority = (entry >> 15) & 1;

			snprintf(line, sizeof(line), "%3u %3u $%03X   %u   %u   %u  %u\n",
				row, col, pattern, palette, priority ? 1 : 0, hflip ? 1 : 0, vflip ? 1 : 0);
			result += line;
		}
	}
	return result;
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolReadVDPState()
{
	IS315_5313* vdp = FindVDP(_systemInterface);
	if (!vdp)
		throw std::runtime_error("VDP device not found");

	std::string result;
	char line[128];

	// Display mode
	bool mode5 = vdp->GetRegisterData(0x01) & 0x04;
	bool displayEnabled = vdp->GetRegisterData(0x01) & 0x40;
	bool h40 = vdp->GetRegisterData(0x0C) & 0x81;
	bool interlace = vdp->GetRegisterData(0x0C) & 0x02;
	bool v30 = vdp->GetRegisterData(0x01) & 0x08;
	snprintf(line, sizeof(line), "Display: %s, %s, %s%s\n",
		displayEnabled ? "ON" : "OFF",
		mode5 ? "Mode 5" : "Mode 4",
		h40 ? "H40 (320px)" : "H32 (256px)",
		v30 ? ", V30 (240px)" : "");
	result += line;

	if (interlace)
		result += "Interlace: ON\n";

	// Plane size
	unsigned int hsz = vdp->GetRegisterData(0x10) & 0x03;
	unsigned int vsz = (vdp->GetRegisterData(0x10) >> 4) & 0x03;
	unsigned int pw = (hsz == 0) ? 32 : (hsz == 1) ? 64 : (hsz == 3) ? 128 : 32;
	unsigned int ph = (vsz == 0) ? 32 : (vsz == 1) ? 64 : (vsz == 3) ? 128 : 32;
	snprintf(line, sizeof(line), "Plane size: %ux%u cells (%ux%u px)\n", pw, ph, pw * 8, ph * 8);
	result += line;

	// Base addresses
	snprintf(line, sizeof(line), "Nametable A:   $%04X\n", (unsigned int)vdp->RegGetNameTableBaseScrollA());
	result += line;
	snprintf(line, sizeof(line), "Nametable B:   $%04X\n", (unsigned int)vdp->RegGetNameTableBaseScrollB());
	result += line;
	snprintf(line, sizeof(line), "Window:        $%04X\n", (unsigned int)vdp->RegGetNameTableBaseWindow());
	result += line;
	snprintf(line, sizeof(line), "Sprite table:  $%04X\n", (unsigned int)vdp->RegGetNameTableBaseSprite());
	result += line;
	snprintf(line, sizeof(line), "HScroll data:  $%04X\n", (unsigned int)vdp->RegGetHScrollDataBase());
	result += line;

	// Scroll mode
	unsigned int reg0B = vdp->GetRegisterData(0x0B);
	const char* hscrollMode;
	unsigned int hmode = reg0B & 0x03;
	if (hmode == 0) hscrollMode = "Full screen";
	else if (hmode == 2) hscrollMode = "Per cell (8px)";
	else if (hmode == 3) hscrollMode = "Per line";
	else hscrollMode = "Invalid";
	bool vscrollPerCell = (reg0B >> 2) & 1;
	snprintf(line, sizeof(line), "HScroll mode:  %s\n", hscrollMode);
	result += line;
	snprintf(line, sizeof(line), "VScroll mode:  %s\n", vscrollPerCell ? "Per 2-cell column" : "Full screen");
	result += line;

	// Background color
	unsigned int bgReg = vdp->GetRegisterData(0x07);
	unsigned int bgPal = (bgReg >> 4) & 0x03;
	unsigned int bgCol = bgReg & 0x0F;
	snprintf(line, sizeof(line), "Background:    Palette %u, Color %u\n", bgPal, bgCol);
	result += line;

	// Auto-increment
	snprintf(line, sizeof(line), "Auto-increment: %u bytes\n", vdp->GetRegisterData(0x0F));
	result += line;

	// DMA
	bool dmaEnabled = vdp->GetRegisterData(0x01) & 0x10;
	snprintf(line, sizeof(line), "DMA:           %s\n", dmaEnabled ? "Enabled" : "Disabled");
	result += line;

	return result;
}

//----------------------------------------------------------------------------------------------------------------------
// Tool implementations - Screenshot & pixel info
//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolScreenshot()
{
	IS315_5313* vdp = FindVDP(_systemInterface);
	if (!vdp)
		throw std::runtime_error("VDP device not found");

	// Use the IDevice::GetScreenshot API to capture the current frame
	IDevice* vdpDevice = dynamic_cast<IDevice*>(vdp);
	if (!vdpDevice)
		throw std::runtime_error("VDP device interface not available");

	Image screenshot;
	if (!vdpDevice->GetScreenshot(screenshot))
		throw std::runtime_error("Failed to capture screenshot");

	unsigned int width = screenshot.GetImageWidth();
	unsigned int height = screenshot.GetImageHeight();

	if (width == 0 || height == 0)
		throw std::runtime_error("Screenshot is empty (no frame rendered yet)");

	// Save PNG to memory buffer
	Stream::Buffer pngBuffer;
	if (!screenshot.SavePNGImage(pngBuffer))
		throw std::runtime_error("Failed to encode PNG");

	// Build temp file path with .png extension
	wchar_t tempPath[MAX_PATH];
	GetTempPathW(MAX_PATH, tempPath);

	wchar_t tempFile[MAX_PATH];
	GetTempFileNameW(tempPath, L"exo", 0, tempFile);

	std::wstring pngPath(tempFile);
	size_t dotPos = pngPath.rfind(L'.');
	if (dotPos != std::wstring::npos)
		pngPath = pngPath.substr(0, dotPos);
	pngPath += L".png";

	// Write raw PNG bytes to file
	std::string narrowPath(pngPath.begin(), pngPath.end());
	FILE* fp = fopen(narrowPath.c_str(), "wb");
	if (!fp)
	{
		DeleteFileW(tempFile);
		throw std::runtime_error("Failed to create temp file for screenshot");
	}
	fwrite(pngBuffer.GetRawBuffer(), 1, (size_t)pngBuffer.Size(), fp);
	fclose(fp);

	// Delete the original .tmp file that GetTempFileName created
	DeleteFileW(tempFile);

	char header[512];
	snprintf(header, sizeof(header), "Screenshot saved: %ux%u\n%s", width, height, narrowPath.c_str());
	return std::string(header);
}

//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolQueryPixel(unsigned int x, unsigned int y)
{
	IS315_5313* vdp = FindVDP(_systemInterface);
	if (!vdp)
		throw std::runtime_error("VDP device not found");

	// Enable full image buffer info if not already active. The VDP only populates
	// per-pixel metadata when this flag is on. After enabling, a new frame must be
	// rendered before data appears, so we tell the caller to retry.
	if (!vdp->GetVideoEnableFullImageBufferInfo())
	{
		vdp->SetVideoEnableFullImageBufferInfo(true);
		throw std::runtime_error("Pixel info was not enabled. It has been turned on now. Run the system for at least one frame and try again.");
	}

	unsigned int planeNo = vdp->GetImageCompletedBufferPlaneNo();
	vdp->LockImageBufferData(planeNo);

	unsigned int lineCount = vdp->GetImageBufferLineCount(planeNo);
	unsigned int lineWidth = (lineCount > 0) ? vdp->GetImageBufferLineWidth(planeNo, 0) : 0;

	if (x >= lineWidth || y >= lineCount)
	{
		vdp->UnlockImageBufferData(planeNo);
		char msg[128];
		snprintf(msg, sizeof(msg), "Coordinates (%u,%u) out of range. Frame size: %ux%u", x, y, lineWidth, lineCount);
		throw std::runtime_error(msg);
	}

	const IS315_5313::ImageBufferInfo* info = vdp->GetImageBufferInfo(planeNo, y, x);
	if (!info)
	{
		vdp->UnlockImageBufferData(planeNo);
		throw std::runtime_error("No pixel info available at this coordinate");
	}

	// Copy data before unlocking
	IS315_5313::PixelSource source = info->pixelSource;
	unsigned int hcounter = info->hcounter;
	unsigned int vcounter = info->vcounter;
	unsigned int palRow = info->paletteRow;
	unsigned int palEntry = info->paletteEntry;
	bool shEnabled = info->shadowHighlightEnabled;
	bool shadowed = info->pixelIsShadowed;
	bool highlighted = info->pixelIsHighlighted;
	unsigned int r = info->colorComponentR;
	unsigned int g = info->colorComponentG;
	unsigned int b = info->colorComponentB;
	unsigned int mappingAddr = info->mappingVRAMAddress;
	unsigned int mappingVal = info->mappingData.GetData();
	unsigned int patRow = info->patternRowNo;
	unsigned int patCol = info->patternColumnNo;
	unsigned int sprEntry = info->spriteTableEntryNo;
	unsigned int sprAddr = info->spriteTableEntryAddress;
	unsigned int sprCellW = info->spriteCellWidth;
	unsigned int sprCellH = info->spriteCellHeight;
	unsigned int sprCellX = info->spriteCellPosX;
	unsigned int sprCellY = info->spriteCellPosY;

	vdp->UnlockImageBufferData(planeNo);

	// Convert raw VDP color components to 8-bit display values, applying shadow/highlight
	unsigned char dispR = vdp->ColorValueTo8BitValue(r, shadowed, highlighted);
	unsigned char dispG = vdp->ColorValueTo8BitValue(g, shadowed, highlighted);
	unsigned char dispB = vdp->ColorValueTo8BitValue(b, shadowed, highlighted);

	// Format source name
	const char* sourceName;
	switch (source)
	{
		case IS315_5313::PixelSource::Sprite:     sourceName = "Sprite"; break;
		case IS315_5313::PixelSource::LayerA:     sourceName = "Layer A"; break;
		case IS315_5313::PixelSource::LayerB:     sourceName = "Layer B"; break;
		case IS315_5313::PixelSource::Background: sourceName = "Background"; break;
		case IS315_5313::PixelSource::Window:     sourceName = "Window"; break;
		case IS315_5313::PixelSource::CRAMWrite:  sourceName = "CRAM Write"; break;
		case IS315_5313::PixelSource::Border:     sourceName = "Border"; break;
		case IS315_5313::PixelSource::Blanking:   sourceName = "Blanking"; break;
		default:                                  sourceName = "Unknown"; break;
	}

	std::string result;
	char line[128];

	snprintf(line, sizeof(line), "Pixel (%u,%u)\n", x, y);
	result += line;
	snprintf(line, sizeof(line), "Source: %s\n", sourceName);
	result += line;
	snprintf(line, sizeof(line), "Color: R=%u G=%u B=%u (#%02X%02X%02X)\n", dispR, dispG, dispB, dispR, dispG, dispB);
	result += line;
	snprintf(line, sizeof(line), "Palette: row %u, entry %u\n", palRow, palEntry);
	result += line;
	snprintf(line, sizeof(line), "HCounter=%u VCounter=%u\n", hcounter, vcounter);
	result += line;

	if (shEnabled)
	{
		snprintf(line, sizeof(line), "Shadow/Highlight: %s\n",
			shadowed ? "Shadowed" : (highlighted ? "Highlighted" : "Normal"));
		result += line;
	}

	// Tile mapping info (relevant for LayerA, LayerB, Window, Sprite)
	bool hasMappingData = (source == IS315_5313::PixelSource::LayerA ||
		source == IS315_5313::PixelSource::LayerB ||
		source == IS315_5313::PixelSource::Window ||
		source == IS315_5313::PixelSource::Sprite);

	if (hasMappingData)
	{
		unsigned int tileIndex = mappingVal & 0x7FF;
		bool hflip = (mappingVal >> 11) & 1;
		bool vflip = (mappingVal >> 12) & 1;
		bool priority = (mappingVal >> 15) & 1;

		snprintf(line, sizeof(line), "Mapping VRAM addr: $%04X\n", mappingAddr);
		result += line;
		snprintf(line, sizeof(line), "Mapping data: $%04X\n", mappingVal);
		result += line;
		snprintf(line, sizeof(line), "Tile: $%03X  Pal: %u  Pri: %u  HF: %u  VF: %u\n",
			tileIndex, palRow, priority ? 1 : 0, hflip ? 1 : 0, vflip ? 1 : 0);
		result += line;
		snprintf(line, sizeof(line), "Pattern pos: row %u, col %u\n", patRow, patCol);
		result += line;
		snprintf(line, sizeof(line), "Tile data VRAM addr: $%04X\n", tileIndex * 0x20);
		result += line;
	}

	// Sprite-specific info
	if (source == IS315_5313::PixelSource::Sprite)
	{
		snprintf(line, sizeof(line), "Sprite entry: #%u (table addr $%04X)\n", sprEntry, sprAddr);
		result += line;
		snprintf(line, sizeof(line), "Sprite cell: %ux%u at (%u,%u)\n", sprCellW, sprCellH, sprCellX, sprCellY);
		result += line;
	}

	return result;
}

//----------------------------------------------------------------------------------------------------------------------
// Tool implementations - Execution
//----------------------------------------------------------------------------------------------------------------------
std::string MCPServer::ToolStepDevice(const std::string& deviceName)
{
	IDevice* device = FindDeviceByName(deviceName);
	if (!device)
		throw std::runtime_error("Device not found: " + deviceName);

	_systemInterface.ExecuteDeviceStep(device);

	IProcessor* processor = dynamic_cast<IProcessor*>(device);
	json result;
	if (processor)
	{
		char pcStr[16];
		snprintf(pcStr, sizeof(pcStr), "$%06X", processor->GetCurrentPC());
		result["pc"] = pcStr;
	}
	result["status"] = "stepped";
	return result.dump(2);
}

//----------------------------------------------------------------------------------------------------------------------
// Helper functions
//----------------------------------------------------------------------------------------------------------------------
IDevice* MCPServer::FindDeviceByName(const std::string& name)
{
	std::wstring wname(name.begin(), name.end());
	std::list<IDevice*> devices = _systemInterface.GetLoadedDevices();
	for (IDevice* device : devices)
	{
		if (device->GetDeviceInstanceName() == wname)
			return device;
	}
	return nullptr;
}

//----------------------------------------------------------------------------------------------------------------------
// Accepts: "$00FF0000", "0x00FF0000", "0X00ff0000", "00FF0000h", "00ff0000H", or plain integer 16711680
unsigned int MCPServer::ParseAddress(const std::string& str)
{
	if (str.empty())
		throw std::runtime_error("Empty address string");

	std::string s = str;

	// $XXXX (Motorola convention)
	if (s[0] == '$')
	{
		return (unsigned int)strtoul(s.c_str() + 1, nullptr, 16);
	}
	// 0xXXXX or 0XXXXX
	if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
	{
		return (unsigned int)strtoul(s.c_str() + 2, nullptr, 16);
	}
	// XXXXh or XXXXH (Zilog convention)
	if (s.back() == 'h' || s.back() == 'H')
	{
		s.pop_back();
		return (unsigned int)strtoul(s.c_str(), nullptr, 16);
	}
	// Plain decimal
	return (unsigned int)strtoul(s.c_str(), nullptr, 10);
}
