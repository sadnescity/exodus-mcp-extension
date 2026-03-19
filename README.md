# Exodus MCP Extension

MCP (Model Context Protocol) server extension for the [Exodus Emulation Platform](http://www.exodusemulator.com). Exposes 26 tools for Sega Mega Drive/Genesis debugging and reverse engineering via Streamable HTTP.

## Setup

1. Build the solution (`ExodusMCP.sln`) in Visual Studio 2022 (x64 Release)
2. Copy `ExodusMCP.dll` from `bin/Release/` to the Exodus `Assemblies` folder
3. Load a Mega Drive system module in Exodus — the MCP server starts automatically on port 8600

## Connection

- **Endpoint:** `POST http://localhost:8600/mcp`
- **Protocol:** JSON-RPC 2.0 (MCP Streamable HTTP, protocol version `2024-11-05`)
- **Binding:** localhost only

## Tools (26)

### System (3)

| Tool | Description |
|------|-------------|
| `get_system_status` | Running/stopped state and loaded modules |
| `run_system` | Start or resume emulation |
| `stop_system` | Pause emulation |

### Devices (1)

| Tool | Description |
|------|-------------|
| `list_devices` | List all loaded devices with class and instance names |

### CPU (4)

| Tool | Parameters | Description |
|------|-----------|-------------|
| `read_cpu_registers` | `device` | Full register dump (M68000: D0-D7, A0-A7, PC, SR, SSP, USP) |
| `read_memory` | `device`, `address`, `length` | Read bytes from CPU address space (max 4096) |
| `write_memory` | `device`, `address`, `data` | Write bytes to CPU address space |
| `disassemble` | `device`, `address`, `count?` | Disassemble instructions (default 10) |

### Memory Search (1)

| Tool | Parameters | Description |
|------|-----------|-------------|
| `search_memory` | `device`, `hex`, `start?`, `end?` | Search for hex byte pattern (max 64 results) |

### Breakpoints (3)

| Tool | Parameters | Description |
|------|-----------|-------------|
| `set_breakpoint` | `device`, `address` | Set execution breakpoint |
| `remove_breakpoint` | `device`, `address` | Remove breakpoint |
| `list_breakpoints` | `device` | List active breakpoints |

### Watchpoints (3)

| Tool | Parameters | Description |
|------|-----------|-------------|
| `set_watchpoint` | `device`, `address`, `size?`, `read?`, `write?` | Set memory watchpoint (bus-level, works on I/O ports) |
| `remove_watchpoint` | `device`, `address` | Remove watchpoint |
| `list_watchpoints` | `device` | List active watchpoints |

### Execution (1)

| Tool | Parameters | Description |
|------|-----------|-------------|
| `step_device` | `device` | Single-step one instruction |

### VDP Raw Memory (4)

No `device` parameter — these access VDP hardware directly.

| Tool | Parameters | Description |
|------|-----------|-------------|
| `read_vram` | `address`, `length` | Read raw VRAM (64 KB) |
| `read_cram` | `address`, `length` | Read raw CRAM (128 bytes) |
| `read_vsram` | `address`, `length` | Read raw VSRAM (80 bytes) |
| `read_vdp_registers` | — | Read all 24 VDP registers |

### VDP Decoded (4)

No `device` parameter.

| Tool | Parameters | Description |
|------|-----------|-------------|
| `read_sprite_table` | — | Decode sprite attribute table (up to 80 sprites) |
| `read_palette` | — | All 64 colors as 8-bit RGB |
| `read_nametable` | `plane`, `row_start?`, `row_count?` | Decode plane nametable (a/b/window) |
| `read_vdp_state` | — | Full VDP configuration |

### Screenshot & Pixel Info (2)

No `device` parameter.

| Tool | Parameters | Description |
|------|-----------|-------------|
| `screenshot` | — | Capture frame as PNG temp file, returns file path |
| `query_pixel` | `x`, `y` | Per-pixel rendering info: source layer, tile, palette, color, sprite data |

`query_pixel` automatically enables the VDP's pixel info buffer on first call. One frame must render before data is available.

## Address Format

All tools accept addresses as:

| Format | Example | Notes |
|--------|---------|-------|
| Motorola | `$FF0000` | Preferred for M68000 |
| C-style | `0xFF0000` | |
| Zilog | `FF0000h` | For Z80 |
| Integer | `16711680` | Decimal |

## Architecture

```
ExodusMCPExtension.cpp    Extension lifecycle, server thread, settings
MCPServer.cpp             HTTP server, MCP protocol, all 26 tool implementations
MCPServer.h               Class definition
interface.cpp             DLL exports
```

The extension loads as a standard Exodus DLL plugin. On `BuildExtension()`, it spawns a thread running an HTTP server (cpp-httplib) that handles JSON-RPC 2.0 requests on the `/mcp` endpoint.

## Dependencies

- **Exodus SDK:** Extension, Processor, GenericAccess, WindowsSupport, Stream, HierarchicalStorage, Image
- **Exodus devices:** IS315_5313 (VDP), IM68000 (CPU)
- **Third-party:** [cpp-httplib](https://github.com/yhirose/cpp-httplib), [nlohmann/json](https://github.com/nlohmann/json)

## Building

### Prerequisites

- **Visual Studio 2022** (v143 platform toolset, Desktop C++ workload)
- **C++17** or later
- **Exodus source tree** — cloned and built (headers + static libraries)

### Directory Layout

The build expects Exodus at `../Exodus` relative to this repo (sibling directories):

```
Dev/
  Exodus/                  # Exodus emulator source
  exodus-mcp-extension/    # This repo
```

This can be overridden with the `ExodusDir` MSBuild property.

### Step 1: Build Exodus Dependencies

The MCP extension links against Exodus SDK libraries. Build them first from the Exodus source tree:

```powershell
# Build third-party libraries (zlib, libpng, libjpeg, libtiff, expat)
msbuild Exodus\ThirdPartyLibraries.sln /p:Configuration=Release /p:Platform=x64 /m

# Build required SDK libraries
msbuild Exodus\Exodus.sln /p:Configuration=Release /p:Platform=x64 /m ^
  /t:Extension;Processor;GenericAccess;WindowsSupport;Stream;HierarchicalStorage;Image
```

This produces static libraries in `Exodus/x64/Output/Release/*/`.

**Required libraries:**

| Library | Source | Purpose |
|---------|--------|---------|
| Extension.lib | ExodusSDK/Extension | Extension plugin interface |
| Processor.lib | ExodusSDK/Processor | CPU/processor interfaces, breakpoints, watchpoints |
| GenericAccess.lib | ExodusSDK/GenericAccess | Generic device data access |
| WindowsSupport.lib | Support Libraries | Windows API helpers |
| Stream.lib | Support Libraries | File and buffer I/O |
| HierarchicalStorage.lib | Support Libraries | XML settings persistence |
| Image.lib | Support Libraries | PNG encoding for screenshots |

**Required third-party libraries** (built by `ThirdPartyLibraries.sln`):

| Library | Purpose |
|---------|---------|
| zlibx64.lib | Compression (used by libpng) |
| libpngx64.lib | PNG image encoding |
| libjpegx64.lib | JPEG support (Image.lib dependency) |
| libtiffx64.lib | TIFF support (Image.lib dependency) |

### Step 2: Build the MCP Extension

```powershell
# From the exodus-mcp-extension directory
msbuild ExodusMCP.vcxproj /p:Configuration=Release /p:Platform=x64 /m
```

To use a custom Exodus path:

```powershell
msbuild ExodusMCP.vcxproj /p:Configuration=Release /p:Platform=x64 /p:ExodusDir=C:\path\to\Exodus\ /m
```

Output: `bin/Release/ExodusMCP.dll`

### Step 3: Install

Copy `ExodusMCP.dll` to the Exodus `Assemblies` folder (or wherever `AssembliesPath` points in `settings.xml`).

### Building from Visual Studio

1. Open `ExodusMCP.sln` in Visual Studio 2022
2. Ensure Exodus is built (Step 1)
3. Select **Release | x64**
4. Build Solution (Ctrl+Shift+B)

### CI/CD

The repository includes a GitHub Actions workflow (`.github/workflows/build.yml`) that:

1. Checks out Exodus and builds the required SDK libraries
2. Builds the MCP extension DLL
3. Uploads `ExodusMCP.dll` as a build artifact
4. On tagged releases, attaches the DLL to the GitHub release

Releases are triggered by publishing a GitHub release (tag).

## Versioning

Versions follow [semantic versioning](https://semver.org/) and are driven by git tags.

- **Releases:** Tag with `v1.0.0`, create a GitHub release — the CI workflow builds and attaches the DLL
- **CI builds:** The version from the tag is injected at compile time via the `MCPVersion` MSBuild property
- **Local builds:** Without `MCPVersion`, the version falls back to the hardcoded default in `MCPServer.cpp`

To build with a specific version locally:

```powershell
msbuild ExodusMCP.vcxproj /p:Configuration=Release /p:Platform=x64 /p:MCPVersion=1.2.3
```

The version is reported in the MCP `initialize` response under `serverInfo.version`.

## Configuration

The server port (default 8600) is configurable via the extension settings in the Exodus module XML:

```xml
<MCPPort>8600</MCPPort>
```

## License

Same license as the Exodus Emulation Platform.
