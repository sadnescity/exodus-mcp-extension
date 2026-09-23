#include "MCPProtocol.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

namespace MCPProtocol
{

//----------------------------------------------------------------------------------------------------------------------
// Constants
//----------------------------------------------------------------------------------------------------------------------
static const char* const MetaProtocolVersion = "io.modelcontextprotocol/protocolVersion";
static const char* const MetaClientCapabilities = "io.modelcontextprotocol/clientCapabilities";
static const char* const MetaServerInfo = "io.modelcontextprotocol/serverInfo";

// JSON-RPC and MCP error codes
enum ErrorCode
{
	ParseError = -32700,
	InvalidRequest = -32600,
	MethodNotFound = -32601,
	InvalidParams = -32602,
	HeaderMismatch = -32020,
	UnsupportedProtocolVersion = -32022,
};

// The tool list is fixed for the lifetime of the DLL, so clients may cache list results for a while
static const unsigned int CacheTtlMs = 300000;

//----------------------------------------------------------------------------------------------------------------------
// Protocol revisions
//----------------------------------------------------------------------------------------------------------------------
const std::vector<std::string>& ModernVersions()
{
	static const std::vector<std::string> versions = { "2026-07-28" };
	return versions;
}

//----------------------------------------------------------------------------------------------------------------------
const std::vector<std::string>& LegacyVersions()
{
	static const std::vector<std::string> versions = { "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05" };
	return versions;
}

//----------------------------------------------------------------------------------------------------------------------
std::vector<std::string> SupportedVersions()
{
	std::vector<std::string> versions = ModernVersions();
	versions.insert(versions.end(), LegacyVersions().begin(), LegacyVersions().end());
	return versions;
}

//----------------------------------------------------------------------------------------------------------------------
static bool Contains(const std::vector<std::string>& list, const std::string& value)
{
	return std::find(list.begin(), list.end(), value) != list.end();
}

//----------------------------------------------------------------------------------------------------------------------
// Header helpers
//----------------------------------------------------------------------------------------------------------------------
static bool Base64Decode(const std::string& input, std::string& output)
{
	auto decodeChar = [](char c) -> int {
		if (c >= 'A' && c <= 'Z') return c - 'A';
		if (c >= 'a' && c <= 'z') return c - 'a' + 26;
		if (c >= '0' && c <= '9') return c - '0' + 52;
		if (c == '+') return 62;
		if (c == '/') return 63;
		return -1;
	};

	if ((input.size() % 4) != 0)
		return false;

	output.clear();
	for (size_t i = 0; i < input.size(); i += 4)
	{
		int values[4];
		int padding = 0;
		for (int j = 0; j < 4; ++j)
		{
			char c = input[i + j];
			if (c == '=')
			{
				// Padding is only valid in the last two positions of the final group
				if ((i + 4 != input.size()) || (j < 2))
					return false;
				values[j] = 0;
				++padding;
			}
			else
			{
				if (padding > 0)
					return false;
				values[j] = decodeChar(c);
				if (values[j] < 0)
					return false;
			}
		}
		unsigned int triple = (values[0] << 18) | (values[1] << 12) | (values[2] << 6) | values[3];
		output.push_back((char)((triple >> 16) & 0xFF));
		if (padding < 2)
			output.push_back((char)((triple >> 8) & 0xFF));
		if (padding < 1)
			output.push_back((char)(triple & 0xFF));
	}
	return true;
}

//----------------------------------------------------------------------------------------------------------------------
bool DecodeHeaderValue(const std::string& raw, std::string& decoded)
{
	static const std::string prefix = "=?base64?";
	static const std::string suffix = "?=";
	if ((raw.size() >= prefix.size() + suffix.size()) && (raw.compare(0, prefix.size(), prefix) == 0) && (raw.compare(raw.size() - suffix.size(), suffix.size(), suffix) == 0))
	{
		return Base64Decode(raw.substr(prefix.size(), raw.size() - prefix.size() - suffix.size()), decoded);
	}

	// Plain values must be visible ASCII, space or tab
	for (unsigned char c : raw)
	{
		if ((c != '\t') && ((c < 0x20) || (c > 0x7E)))
			return false;
	}
	decoded = raw;
	return true;
}

//----------------------------------------------------------------------------------------------------------------------
bool IsLocalOrigin(const std::string& origin)
{
	std::string rest;
	if (origin.compare(0, 7, "http://") == 0)
		rest = origin.substr(7);
	else if (origin.compare(0, 8, "https://") == 0)
		rest = origin.substr(8);
	else
		return false;

	std::transform(rest.begin(), rest.end(), rest.begin(), [](unsigned char c) { return (char)std::tolower(c); });

	static const char* const hosts[] = { "localhost", "127.0.0.1", "[::1]" };
	for (const char* host : hosts)
	{
		std::string hostStr(host);
		if (rest.compare(0, hostStr.size(), hostStr) != 0)
			continue;
		std::string tail = rest.substr(hostStr.size());
		if (tail.empty() || (tail == "/"))
			return true;
		if ((tail[0] == ':') && (tail.size() > 1))
		{
			size_t i = 1;
			while ((i < tail.size()) && std::isdigit((unsigned char)tail[i]))
				++i;
			if ((i > 1) && ((i == tail.size()) || ((i + 1 == tail.size()) && (tail[i] == '/'))))
				return true;
		}
	}
	return false;
}

//----------------------------------------------------------------------------------------------------------------------
// Response builders
//----------------------------------------------------------------------------------------------------------------------
static HttpReply JsonReply(int status, const json& message)
{
	HttpReply reply;
	reply.status = status;
	reply.body = message.dump();
	reply.contentType = "application/json";
	return reply;
}

//----------------------------------------------------------------------------------------------------------------------
static HttpReply ErrorReply(int status, const json& id, int code, const std::string& message, const json& data = json())
{
	json response;
	response["jsonrpc"] = "2.0";
	response["id"] = id;
	response["error"]["code"] = code;
	response["error"]["message"] = message;
	if (!data.is_null())
		response["error"]["data"] = data;
	return JsonReply(status, response);
}

//----------------------------------------------------------------------------------------------------------------------
static HttpReply ResultReply(const json& id, const json& result)
{
	json response;
	response["jsonrpc"] = "2.0";
	response["id"] = id;
	response["result"] = result;
	return JsonReply(200, response);
}

//----------------------------------------------------------------------------------------------------------------------
static json ServerInfoObject(const ServerInfo& serverInfo)
{
	json info;
	info["name"] = serverInfo.name;
	info["version"] = serverInfo.version;
	return info;
}

//----------------------------------------------------------------------------------------------------------------------
// Adds the fields every modern result carries
static void AddModernResultFields(json& result, const ServerInfo& serverInfo)
{
	result["resultType"] = "complete";
	result["_meta"][MetaServerInfo] = ServerInfoObject(serverInfo);
}

//----------------------------------------------------------------------------------------------------------------------
// Method handlers shared by both eras
//----------------------------------------------------------------------------------------------------------------------
static json DiscoverResult(const ServerInfo& serverInfo)
{
	json result;
	result["supportedVersions"] = SupportedVersions();
	result["capabilities"]["tools"] = json::object();
	if (!serverInfo.instructions.empty())
		result["instructions"] = serverInfo.instructions;
	result["ttlMs"] = CacheTtlMs;
	result["cacheScope"] = "public";
	AddModernResultFields(result, serverInfo);
	return result;
}

//----------------------------------------------------------------------------------------------------------------------
static HttpReply HandleToolsCall(const json& id, const json& params, const Handlers& handlers, bool modern, const ServerInfo& serverInfo)
{
	if (!params.contains("name") || !params["name"].is_string())
		return ErrorReply(modern ? 400 : 200, id, InvalidParams, "Missing required parameter: name");

	std::string toolName = params["name"].get<std::string>();
	json arguments = json::object();
	if (params.contains("arguments") && params["arguments"].is_object())
		arguments = params["arguments"];

	ToolCallOutcome outcome;
	try
	{
		outcome = handlers.callTool(toolName, arguments);
	}
	catch (const std::exception& ex)
	{
		outcome.status = ToolCallStatus::ToolError;
		outcome.text = ex.what();
	}
	catch (...)
	{
		outcome.status = ToolCallStatus::ToolError;
		outcome.text = "Unknown error";
	}

	if (outcome.status == ToolCallStatus::UnknownTool)
		return ErrorReply(200, id, InvalidParams, "Unknown tool: " + toolName);

	json result;
	json contentItem;
	contentItem["type"] = "text";
	contentItem["text"] = (outcome.status == ToolCallStatus::ToolError) ? ("Error: " + outcome.text) : outcome.text;
	result["content"] = json::array({ contentItem });
	if (outcome.status == ToolCallStatus::ToolError)
		result["isError"] = true;
	if (modern)
		AddModernResultFields(result, serverInfo);
	return ResultReply(id, result);
}

//----------------------------------------------------------------------------------------------------------------------
static HttpReply HandleToolsList(const json& id, const Handlers& handlers, bool modern, const ServerInfo& serverInfo)
{
	json result;
	result["tools"] = handlers.listTools();
	if (modern)
	{
		result["ttlMs"] = CacheTtlMs;
		result["cacheScope"] = "public";
		AddModernResultFields(result, serverInfo);
	}
	return ResultReply(id, result);
}

//----------------------------------------------------------------------------------------------------------------------
// Modern (stateless, per-request _meta) requests
//----------------------------------------------------------------------------------------------------------------------
static HttpReply HandleModernRequest(const json& id, const std::string& method, const json& params, const std::string& requestedVersion, const HeaderLookup& getHeader, const ServerInfo& serverInfo, const Handlers& handlers)
{
	// The MCP-Protocol-Version header must mirror _meta
	std::string headerVersion;
	if (!getHeader("MCP-Protocol-Version", headerVersion))
		return ErrorReply(400, id, HeaderMismatch, "Header mismatch: missing MCP-Protocol-Version header");
	if (headerVersion != requestedVersion)
		return ErrorReply(400, id, HeaderMismatch, "Header mismatch: MCP-Protocol-Version header value '" + headerVersion + "' does not match body value '" + requestedVersion + "'");

	if (!Contains(ModernVersions(), requestedVersion))
	{
		json data;
		data["supported"] = SupportedVersions();
		data["requested"] = requestedVersion;
		return ErrorReply(400, id, UnsupportedProtocolVersion, "Unsupported protocol version", data);
	}

	const json& meta = params["_meta"];
	if (!meta.contains(MetaClientCapabilities) || !meta[MetaClientCapabilities].is_object())
		return ErrorReply(400, id, InvalidParams, std::string("Missing required _meta field: ") + MetaClientCapabilities);

	// Mcp-Method must mirror the method
	std::string headerMethod;
	if (!getHeader("Mcp-Method", headerMethod))
		return ErrorReply(400, id, HeaderMismatch, "Header mismatch: missing Mcp-Method header");
	if (headerMethod != method)
		return ErrorReply(400, id, HeaderMismatch, "Header mismatch: Mcp-Method header value '" + headerMethod + "' does not match body value '" + method + "'");

	if (method == "server/discover")
	{
		return ResultReply(id, DiscoverResult(serverInfo));
	}
	else if (method == "tools/list")
	{
		return HandleToolsList(id, handlers, true, serverInfo);
	}
	else if (method == "tools/call")
	{
		if (!params.contains("name") || !params["name"].is_string())
			return ErrorReply(400, id, InvalidParams, "Missing required parameter: name");

		// Mcp-Name must mirror params.name
		std::string rawName;
		std::string headerName;
		std::string bodyName = params["name"].get<std::string>();
		if (!getHeader("Mcp-Name", rawName))
			return ErrorReply(400, id, HeaderMismatch, "Header mismatch: missing Mcp-Name header");
		if (!DecodeHeaderValue(rawName, headerName))
			return ErrorReply(400, id, HeaderMismatch, "Header mismatch: Mcp-Name header value is malformed");
		if (headerName != bodyName)
			return ErrorReply(400, id, HeaderMismatch, "Header mismatch: Mcp-Name header value '" + headerName + "' does not match body value '" + bodyName + "'");

		return HandleToolsCall(id, params, handlers, true, serverInfo);
	}

	return ErrorReply(404, id, MethodNotFound, "Method not found: " + method);
}

//----------------------------------------------------------------------------------------------------------------------
// Legacy (initialize handshake) requests
//----------------------------------------------------------------------------------------------------------------------
static HttpReply HandleLegacyRequest(const json& id, const std::string& method, const json& params, const ServerInfo& serverInfo, const Handlers& handlers)
{
	if (method == "initialize")
	{
		// Echo the client's version if we support it, otherwise offer our latest legacy version
		std::string requested = params.value("protocolVersion", "");
		std::string negotiated = Contains(LegacyVersions(), requested) ? requested : LegacyVersions().front();

		json result;
		result["protocolVersion"] = negotiated;
		result["capabilities"]["tools"] = json::object();
		result["serverInfo"] = ServerInfoObject(serverInfo);
		if (!serverInfo.instructions.empty() && (negotiated != "2024-11-05"))
			result["instructions"] = serverInfo.instructions;
		return ResultReply(id, result);
	}
	else if (method == "ping")
	{
		return ResultReply(id, json::object());
	}
	else if (method == "server/discover")
	{
		return ResultReply(id, DiscoverResult(serverInfo));
	}
	else if (method == "tools/list")
	{
		return HandleToolsList(id, handlers, false, serverInfo);
	}
	else if (method == "tools/call")
	{
		return HandleToolsCall(id, params, handlers, false, serverInfo);
	}

	// Legacy clients predate the 404 rule for unknown methods, so keep the JSON-RPC error on a 200
	return ErrorReply(200, id, MethodNotFound, "Method not found: " + method);
}

//----------------------------------------------------------------------------------------------------------------------
// Entry point
//----------------------------------------------------------------------------------------------------------------------
HttpReply HandlePost(const std::string& body, const HeaderLookup& getHeader, const ServerInfo& serverInfo, const Handlers& handlers)
{
	// DNS rebinding protection: reject browser requests from non-local origins
	std::string origin;
	if (getHeader("Origin", origin) && !IsLocalOrigin(origin))
	{
		json response;
		response["jsonrpc"] = "2.0";
		response["error"]["code"] = InvalidRequest;
		response["error"]["message"] = "Forbidden origin: " + origin;
		return JsonReply(403, response);
	}

	json message = json::parse(body, nullptr, false);
	if (message.is_discarded())
		return ErrorReply(400, nullptr, ParseError, "Parse error");
	if (message.is_array())
		return ErrorReply(400, nullptr, InvalidRequest, "Invalid Request: JSON-RPC batches are not supported");
	if (!message.is_object())
		return ErrorReply(400, nullptr, InvalidRequest, "Invalid Request");

	// Client responses and notifications are accepted without a body. The server never sends requests, and the only
	// client notifications (notifications/initialized, notifications/cancelled) need no action here.
	if (!message.contains("method"))
	{
		if (message.contains("result") || message.contains("error"))
			return HttpReply{ 202, "", "" };
		return ErrorReply(400, message.value("id", json()), InvalidRequest, "Invalid Request: missing method");
	}
	if (!message["method"].is_string())
		return ErrorReply(400, message.value("id", json()), InvalidRequest, "Invalid Request: method must be a string");
	if (!message.contains("id"))
		return HttpReply{ 202, "", "" };

	json id = message["id"];
	if (!id.is_string() && !id.is_number_integer())
		return ErrorReply(400, nullptr, InvalidRequest, "Invalid Request: id must be a string or integer");

	std::string method = message["method"].get<std::string>();
	json params = json::object();
	if (message.contains("params"))
	{
		if (!message["params"].is_object())
			return ErrorReply(400, id, InvalidParams, "Invalid params: params must be an object");
		params = message["params"];
	}

	// A request is modern if it declares a non-legacy protocol version in _meta. Legacy versions have no per-request
	// version, so a legacy version found there is served with legacy semantics.
	std::string metaVersion;
	if (params.contains("_meta") && params["_meta"].is_object())
	{
		const json& meta = params["_meta"];
		if (meta.contains(MetaProtocolVersion) && meta[MetaProtocolVersion].is_string())
			metaVersion = meta[MetaProtocolVersion].get<std::string>();
	}
	if (!metaVersion.empty() && !Contains(LegacyVersions(), metaVersion))
		return HandleModernRequest(id, method, params, metaVersion, getHeader, serverInfo, handlers);

	// No modern _meta. A header naming a version we don't serve this way is an error: a modern version means the body
	// is missing its required _meta field, anything else is unsupported. Requests without the header are pre-2025-06-18.
	std::string headerVersion;
	if (metaVersion.empty() && getHeader("MCP-Protocol-Version", headerVersion) && !Contains(LegacyVersions(), headerVersion))
	{
		if (Contains(ModernVersions(), headerVersion))
			return ErrorReply(400, id, InvalidParams, std::string("Missing required _meta field: ") + MetaProtocolVersion);

		json data;
		data["supported"] = SupportedVersions();
		data["requested"] = headerVersion;
		return ErrorReply(400, id, UnsupportedProtocolVersion, "Unsupported protocol version", data);
	}

	return HandleLegacyRequest(id, method, params, serverInfo, handlers);
}

} // namespace MCPProtocol
