# UEInsightMCP — Installation Guide

Step-by-step instructions to get UEInsightMCP running in your project.

---

## Prerequisites

- **Unreal Engine**: UE5.4+ (full features) or UE4.26 RDCSP (trace control only)
- **Python**: 3.10+ (for the MCP bridge)
- **MCP Client**: WorkBuddy, Claude Desktop, Cursor, or any MCP-compatible AI assistant

---

## Step 1: Install the C++ Plugin

Copy the entire `UEInsightMCP/` folder into your project's `Plugins/` directory:

```
YourProject/
└── Plugins/
    ├── UEEditorMCP/        ← optional, for editor automation
    └── UEInsightMCP/       ← this plugin
```

Then regenerate project files:

```bash
# Windows (Visual Studio)
# Right-click your .uproject → "Generate Visual Studio project files"

# Or via command line
"C:\Program Files\Epic Games\UE_5.4\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" \
  -projectfiles -project="YourProject.uproject" -game -rocket -progress
```

Build the project (Development Editor configuration).

### Verify Plugin Loaded

After launching the editor, check the Output Log for:

```
LogInsightMCP: UEInsightMCP: Module starting up
LogInsightMCP: UEInsightMCP: Bridge initializing
LogInsightMCP: UEInsightMCP: Registered 20 action handlers
LogInsightMCP: UEInsightMCP: Server started on port 55559 (max 8 clients)
```

All four lines should appear. If not, see [Troubleshooting](#troubleshooting).

You can also verify in **Edit → Plugins** — search for "UEInsightMCP" and confirm it's enabled.

---

## Step 2: Install Python Dependencies

The Python MCP bridge needs the `mcp` package. Choose one method:

### Option A: pip install (recommended)

```bash
cd <plugin-path>/Python
pip install -e .
```

### Option B: Setup script

```powershell
cd <plugin-path>
.\setup_mcp.ps1
```

### Option C: Use vendor wheels (zero-install)

If UEEditorMCP is already installed in the same `Plugins/` directory, the Python bridge auto-discovers its `vendor/` wheels at startup. No explicit install needed — it falls back to bundled `.whl` files automatically.

**Dependency resolution order:**
1. System-installed `mcp` package (pip/uv)
2. `UEInsightMCP/Python/vendor/*.whl`
3. `UEEditorMCP/Python/vendor/*.whl` (sibling plugin)

---

## Step 3: Configure Your MCP Client

Add UEInsightMCP to your AI assistant's MCP configuration.

### WorkBuddy

Edit `~/.workbuddy/mcp.json`:

```json
{
  "mcpServers": {
    "ue-insight-mcp": {
      "command": "python",
      "args": ["-m", "ue_insight_mcp.server_unified"],
      "cwd": "C:/Users/<you>/Documents/Unreal Projects/<YourProject>/Plugins/UEInsightMCP/Python"
    }
  }
}
```

### Claude Desktop

Edit `~/Library/Application Support/Claude/claude_desktop_config.json` (macOS) or `%APPDATA%/Claude/claude_desktop_config.json` (Windows):

```json
{
  "mcpServers": {
    "ue-insight-mcp": {
      "command": "python",
      "args": ["-m", "ue_insight_mcp.server_unified"],
      "cwd": "C:/Users/<you>/Documents/Unreal Projects/<YourProject>/Plugins/UEInsightMCP/Python"
    }
  }
}
```

### Cursor

Edit `.cursor/mcp.json` in your project root:

```json
{
  "mcpServers": {
    "ue-insight-mcp": {
      "command": "python",
      "args": ["-m", "ue_insight_mcp.server_unified"],
      "cwd": "./Plugins/UEInsightMCP/Python"
    }
  }
}
```

> **Important**: Replace `<you>` and `<YourProject>` with your actual paths. The `cwd` must point to the `Python/` directory inside the plugin.

---

## Step 4: Verify the Connection

### Quick Test from AI Assistant

In your AI assistant, type:

```
Use insight_ping to test the connection
```

Expected response:
```json
{"status": "ok", "pong": true, "port": 55559}
```

### Manual TCP Test

You can also test the raw TCP connection without the MCP bridge:

```bash
cd <plugin-path>
python test_tcp.py
```

This runs through all Phase 1 actions (ping → trace.status → channels.list → start → bookmark → stop → close).

### Full End-to-End Test

```bash
cd <plugin-path>
python test_all_queries.py
```

This tests all 20 actions: session.list → session.open → all query.* → session.close.

---

## Dual Plugin Setup (Recommended)

For the full AI-assisted development loop, install both MCP plugins:

```
Plugins/
├── UEEditorMCP/        ← port 55558: editor automation (blueprint, assets, code)
└── UEInsightMCP/       ← port 55559: trace & performance analysis
```

MCP config for both:

```json
{
  "mcpServers": {
    "ue-editor-mcp": {
      "command": "python",
      "args": ["-m", "ue_editor_mcp.server"],
      "cwd": "./Plugins/UEEditorMCP/Python"
    },
    "ue-insight-mcp": {
      "command": "python",
      "args": ["-m", "ue_insight_mcp.server_unified"],
      "cwd": "./Plugins/UEInsightMCP/Python"
    }
  }
}
```

Both plugins share the same `vendor/` wheels, so only one copy of dependencies is needed.

---

## UE4 RDCSP Installation

For UE4.26 RDCSP builds, the same plugin source works with two automatic differences:

1. **Build.cs**: `EditorScriptingUtilities` is excluded (not available in UE4)
2. **.uplugin**: Uses `WhitelistPlatforms` instead of `PlatformAllowList`
3. **Phase 2 actions**: Compile as no-ops (return "UE5 required" error)

No manual changes needed — the `#if ENGINE_MAJOR_VERSION >= 5` guards handle everything.

---

## Troubleshooting

### "Server started" log doesn't appear

1. Check **Edit → Plugins** — is UEInsightMCP enabled?
2. Look for build errors in the Output Log (`LogInsightMCP` category)
3. Ensure `UEInsightMCP.uplugin` has `"Type": "Editor"` and `"LoadingPhase": "Default"`

### "Port 55559 is already in use"

Another instance of UE Editor with UEInsightMCP is already running, or another process is using the port.

```powershell
# Find what's using the port
netstat -ano | findstr 55559

# Kill the process if needed
taskkill /PID <pid> /F
```

### Python bridge can't connect

1. Confirm the editor is running and shows the "Server started" log
2. Test manually: `python test_tcp.py`
3. Check that no firewall is blocking `127.0.0.1:55559`
4. Ensure `cwd` in MCP config points to the correct `Python/` directory

### "Module 'mcp' not found"

The `mcp` Python package is not installed and no vendor wheels were found.

```bash
pip install mcp
# Or: pip install -e <plugin-path>/Python
```

### Plugin compiles but no actions register

Check that `TraceSessionActions.h/cpp` and `TraceQueryActions.h/cpp` are included in your build. The `Build.cs` should auto-discover all `.cpp` files in `Private/Actions/`.

---

## Connection Reference

### Python → UE (TCP)

| Parameter | Value |
|-----------|-------|
| Host | `127.0.0.1` |
| Port | `55559` |
| Protocol | 4-byte big-endian length + UTF-8 JSON |
| Timeout | 120s |
| Heartbeat | 5s interval |
| Reconnect | Exponential backoff, max 5 attempts |
| Max reconnect delay | 30s |

### UE TCP Server

| Parameter | Value |
|-----------|-------|
| Listen address | `127.0.0.1:55559` |
| Max clients | 8 |
| Connection timeout | 300s |
| Receive buffer | 1 MB |
| GameThread timeout | 240s |

---

*See [USAGE.md](USAGE.md) for how to use the plugin after installation.*
