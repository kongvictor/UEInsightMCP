# UEInsightMCP

**Unreal Insights MCP Plugin** — Let AI assistants control UE Trace recording and query performance data through the [MCP protocol](https://modelcontextprotocol.io/).

> Version 0.2.0 · Beta · UE5.4+ (full) / UE4.26 RDCSP (trace control only)

---

## Why This Exists

Unreal Insights is powerful but GUI-only — you drag timelines, click tabs, and eyeball anomalies. There's no programmatic API to query trace data.

UEInsightMCP fixes this. It exposes **20 actions** via MCP, so an AI assistant can:

1. **Start/stop** trace recording with precise channel control
2. **Open** any `.utrace` file for analysis
3. **Query** frame times, CPU threads, GPU timing, asset loading, memory, counters, bookmarks, and hitches
4. **Cross-reference** multiple data sources in a single conversation
5. **Generate** structured performance reports with bottleneck identification

What took 30–120 minutes of manual Insights UI work now takes **3–5 minutes** of AI-assisted analysis.

### Real-world Example

```
You:  "Record a trace while I run PIE, then tell me what's slow"
AI:   trace.start → (you play) → trace.stop → session.open → query.loadtime
      → query.cpu_threads → query.hitches
      → "TopDownArena Experience loads 126 packages in 21.3s (79.8% of total).
         Bottleneck: ControlRig CR_Mannequin_FootPlant (4283 exports, 153ms)."
```

---

## Architecture

```
AI Assistant (WorkBuddy / Claude / Cursor / etc.)
    │ MCP (stdio)
    ▼
Python MCP Bridge  ─── 7 MCP tools (search → schema → run)
    │ TCP (port 55559)
    ▼
C++ UE Plugin (UEditorSubsystem)
    ├── InsightServer  ── TCP listener + per-client threads
    ├── InsightBridge  ── Command routing + action dispatch
    ├── InsightContext  ── Trace state + analysis session management
    └── Actions/
        ├── TraceControlActions  ── 6 trace actions + batch_execute
        ├── TraceSessionActions  ── 4 session management actions
        └── TraceQueryActions   ── 9 data query actions
```

Runs alongside [UEEditorMCP](../UEEditorMCP/) (port 55558) in the same editor. Together they form a **Code → Profile → Diagnose → Fix** AI-assisted loop.

### Startup Sequence

```
UE Editor Launch
  │
  ├─ ① FUEInsightMCPModule::StartupModule()      // Module loads, starts log capture
  │
  └─ ② UInsightBridge::Initialize()               // EditorSubsystem auto-created
        ├─ RegisterActions()                       // 20 action handlers registered
        └─ FInsightServer::Start(port 55559)       // TCP server begins accepting
```

The plugin is fully automatic — no manual initialization needed. When you open your project in the editor, the TCP server starts listening immediately.

---

## MCP Tools

The Python bridge exposes **7 MCP tools** following the progressive discovery pattern:

| Tool | Description |
|------|-------------|
| `insight_ping` | Test connection to UE Insight plugin |
| `insight_actions_search` | Search available actions by keyword/tag |
| `insight_actions_schema` | Get full parameter schema + examples for an action |
| `insight_actions_run` | Execute a single action |
| `insight_batch` | Execute multiple actions in one TCP round-trip (max 50) |
| `insight_resources_read` | Read embedded docs (conventions / error_codes / trace_channels) |
| `insight_logs_tail` | Tail UE editor logs |

### Discovery Flow

```
1. insight_actions_search("loadtime")     → find action IDs
2. insight_actions_schema("query.loadtime") → learn parameters
3. insight_actions_run("query.loadtime", {top_n: 50, sort_by: "start_time"}) → get data
```

---

## All 20 Actions

### Trace Control (Phase 1) — 6 actions

| Action | Description | Type |
|--------|-------------|------|
| `trace.start` | Start recording (file or server mode) | write |
| `trace.stop` | Stop recording, return file info | write |
| `trace.status` | Get current trace state | read |
| `trace.channels.list` | List all registered channels + enabled state | read |
| `trace.channels.toggle` | Enable/disable specific channels | write |
| `trace.bookmark.add` | Inject a named bookmark at current time | write |

### Session Management (Phase 2) — 4 actions

| Action | Description | Type |
|--------|-------------|------|
| `session.list` | List `.utrace` files in trace directory | read |
| `session.open` | Open a `.utrace` file for analysis | read |
| `session.info` | Get session details (frames, threads, providers) | read |
| `session.close` | Close session, free resources | write |

### Data Query (Phase 2) — 9 actions

| Action | Description | Type |
|--------|-------------|------|
| `query.frame_times` | Frame time stats: avg/p95/p99, FPS distribution, worst frames | read |
| `query.cpu_threads` | CPU thread timing aggregation with top timers per thread | read |
| `query.gpu_timing` | GPU timing data, top GPU timers by total time | read |
| `query.loadtime` | Asset loading times sorted by duration or start time | read |
| `query.loadtime_deps` | Asset dependency graph with per-package timing | read |
| `query.memory` | LLM memory tags: trackers and latest values | read |
| `query.counters` | Named counters with optional value samples | read |
| `query.bookmarks` | All `TRACE_BOOKMARK` entries from the session | read |
| `query.hitches` | Hitch detection: frames exceeding threshold, sorted by severity | read |

### Utility — 1 action

| Action | Description |
|--------|-------------|
| `batch_execute` | Execute multiple actions in one request |

---

## UE4 / UE5 Compatibility

The same codebase supports both engine versions via `#if ENGINE_MAJOR_VERSION >= 5`:

| Feature | UE5 | UE4 RDCSP |
|---------|-----|-----------|
| **Trace Control** (Phase 1) | ✅ `FTraceAuxiliary` | ✅ `Trace::WriteTo/SendTo/Stop` |
| **Session & Query** (Phase 2) | ✅ Full `IAnalysisSession` API | ❌ Not available |
| Channel enumeration | `UE::Trace::EnumerateChannels` | Hardcoded 20+ known channels |
| Namespace | `UE::Trace::` | `Trace::` |
| EditorScriptingUtilities | ✅ Required | ❌ Conditionally excluded |
| RDCSP extensions | — | `GetConnectPort` / `GetMemoryUsed` |

---

## Technical Details

### TCP Protocol

- **Port**: 55559 (UEEditorMCP uses 55558)
- **Message format**: 4-byte big-endian length header + UTF-8 JSON body
- **Receive buffer**: 1 MB
- **Max clients**: 8
- **Connection timeout**: 300s

### Thread Model

Actions declare `RequiresGameThread()`:
- **Trace control actions** → Game thread (must interact with FTraceAuxiliary)
- **Session & query actions** → TCP worker thread (no game thread blocking)

This prevents heavy queries from stalling the editor.

### Crash Protection

- **Windows**: SEH (`__try / __except`) catches access violations
- **All platforms**: C++ exception catching (`try / catch`)
- Crashes return `crash_prevented` error without taking down the editor

### Analysis Module Activation

Session queries automatically enable UE5 analysis modules (TimingProfiler, LoadTimeProfiler, MemoryProfiler, etc.) via `IModuleService::SetModuleEnabled()` before creating the analysis service. This fixes the common issue where `LoadTimeProfiler` returns nullptr because it's disabled by default when `WITH_EDITOR` is true.

---

## Project Structure

```
UEInsightMCP/
├── UEInsightMCP.uplugin
├── README.md / README_zh.md   ← Project overview (EN / 中文)
├── INSTALL.md / INSTALL_zh.md ← Installation guide (EN / 中文)
├── USAGE.md / USAGE_zh.md     ← Usage guide & workflows (EN / 中文)
├── setup_mcp.ps1
│
├── Source/UEInsightMCP/
│   ├── UEInsightMCP.Build.cs
│   ├── Public/
│   │   ├── UEInsightMCPModule.h
│   │   ├── InsightBridge.h          # Command router (UEditorSubsystem)
│   │   ├── InsightServer.h          # TCP server
│   │   ├── InsightContext.h         # Trace state + analysis session
│   │   ├── InsightLogCapture.h      # Editor log ring buffer
│   │   └── Actions/
│   │       ├── InsightAction.h      # Base class (SEH + thread model)
│   │       ├── TraceControlActions.h
│   │       ├── TraceSessionActions.h
│   │       └── TraceQueryActions.h
│   └── Private/
│       ├── UEInsightMCPModule.cpp
│       ├── InsightBridge.cpp         # Registers all 20 actions
│       ├── InsightServer.cpp
│       ├── InsightContext.cpp
│       ├── InsightLogCapture.cpp
│       └── Actions/
│           ├── InsightAction.cpp
│           ├── TraceControlActions.cpp   # UE4/UE5 dual implementation
│           ├── TraceSessionActions.cpp
│           └── TraceQueryActions.cpp     # 9 query actions (~43KB)
│
└── Python/
    ├── pyproject.toml
    ├── requirements.txt
    └── ue_insight_mcp/
        ├── __init__.py
        ├── connection.py             # Persistent TCP (heartbeat + auto-reconnect)
        ├── server_unified.py         # MCP Server (7 tools)
        ├── registry/
        │   ├── __init__.py           # ActionRegistry + keyword search engine
        │   └── insight_actions.py    # 19 action metadata definitions
        ├── resources/
        │   ├── conventions.md
        │   ├── error_codes.md
        │   └── trace_channels.md
        └── vendor/                   # Pre-packaged .whl dependencies
```

---

## Build Dependencies

| Module | Description |
|--------|-------------|
| Core, CoreUObject, Engine | Base engine |
| Slate, SlateCore | UI framework |
| UnrealEd, EditorSubsystem | Editor framework |
| Json, JsonUtilities | JSON serialization |
| Networking, Sockets | TCP communication |
| TraceLog | Trace namespace/emit API |
| TraceAnalysis | IAnalyzer, trace event parsing |
| TraceServices | IAnalysisSession, analysis session management |
| EditorScriptingUtilities | UE5 only (conditional compilation) |

---

## Roadmap

### Phase 1 — Trace Control ✅

- 6 trace control actions + batch_execute
- TCP Server + Python MCP Bridge
- Dual engine support (UE5 + UE4 RDCSP)

### Phase 2 — Data Query Engine ✅

- 4 session management actions
- 9 data query actions (frame times, CPU, GPU, loadtime, memory, counters, bookmarks, hitches)
- Smart thread model (queries don't block game thread)
- Analysis module auto-activation fix

### Phase 3 — Smart Analysis + Reports (Planned)

- `analyze.bottleneck` — AI-assisted bottleneck diagnosis
- `analyze.regression` — Performance regression detection
- `report.generate` — Auto-generate performance reports

---

## Documentation

| Document | 中文版 | Contents |
|----------|--------|----------|
| [README.md](README.md) | [README_zh.md](README_zh.md) | Project overview, architecture, action reference |
| [INSTALL.md](INSTALL.md) | [INSTALL_zh.md](INSTALL_zh.md) | Step-by-step installation guide |
| [USAGE.md](USAGE.md) | [USAGE_zh.md](USAGE_zh.md) | How to use in the editor, workflows, parameters, troubleshooting |

---

## License

MIT License

## Author

tsuyu
