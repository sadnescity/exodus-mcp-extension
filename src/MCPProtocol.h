#ifndef __MCPPROTOCOL_H__
#define __MCPPROTOCOL_H__
#include "nlohmann/json_fwd.hpp"
#include <functional>
#include <string>
#include <vector>

// MCP protocol layer for the Streamable HTTP transport.
//
// This file deliberately has no dependency on Exodus or on the HTTP library, so the protocol logic can be compiled
// and tested on its own. MCPServer feeds it the raw POST body and a header lookup, and gets back the HTTP status and
// body to send.
//
// The server is dual-era:
// - Modern (2026-07-28 and later): stateless. Every request carries its protocol version and client capabilities in
//   params._meta, mirrored in the MCP-Protocol-Version / Mcp-Method / Mcp-Name headers. server/discover is supported.
// - Legacy (2024-11-05 to 2025-11-25): initialize handshake, then requests without _meta. No session IDs are minted,
//   which every legacy Streamable HTTP revision allows.
namespace MCPProtocol
{
	// Protocol revisions, newest first
	const std::vector<std::string>& ModernVersions();
	const std::vector<std::string>& LegacyVersions();
	std::vector<std::string> SupportedVersions();

	struct ServerInfo
	{
		std::string name;
		std::string version;
		std::string instructions;
	};

	enum class ToolCallStatus
	{
		Success,     // Tool ran, text is its output
		ToolError,   // Tool ran and failed, text is the error message (reported as isError: true)
		UnknownTool, // No tool with that name (reported as a JSON-RPC error)
	};

	struct ToolCallOutcome
	{
		ToolCallStatus status;
		std::string text;
	};

	struct Handlers
	{
		// Returns the JSON array of tool definitions for tools/list
		std::function<nlohmann::json()> listTools;
		// Runs a tool. Exceptions escaping this function are reported as tool errors.
		std::function<ToolCallOutcome(const std::string& name, const nlohmann::json& arguments)> callTool;
	};

	// Looks up an HTTP request header by name (case-insensitive). Returns false if the header is absent.
	typedef std::function<bool(const std::string& name, std::string& value)> HeaderLookup;

	struct HttpReply
	{
		int status;
		std::string body;         // Empty means no body
		std::string contentType;
	};

	// Handles a POST to the MCP endpoint
	HttpReply HandlePost(const std::string& body, const HeaderLookup& getHeader, const ServerInfo& serverInfo, const Handlers& handlers);

	// Returns true if an Origin header value refers to the local machine (DNS rebinding protection)
	bool IsLocalOrigin(const std::string& origin);

	// Decodes a header value that may use the "=?base64?...?=" sentinel encoding. Returns false if it is malformed.
	bool DecodeHeaderValue(const std::string& raw, std::string& decoded);
}

#endif
