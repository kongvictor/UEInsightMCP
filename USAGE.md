# UEInsightMCP — Usage Guide

How to use UEInsightMCP in the Unreal Editor for performance analysis.

> **Prerequisites**: Plugin installed and MCP configured. See [INSTALL.md](INSTALL.md) if not done yet.

---

## Table of Contents

- [Quick Start](#quick-start)
- [How It Works in the Editor](#how-it-works-in-the-editor)
- [Workflow 1: Live Performance Profiling](#workflow-1-live-performance-profiling)
- [Workflow 2: Offline Trace Analysis](#workflow-2-offline-trace-analysis)
- [Workflow 3: Asset Loading Analysis](#workflow-3-asset-loading-analysis)
- [Workflow 4: Quick Health Check](#workflow-4-quick-health-check)
- [Action Parameter Reference](#action-parameter-reference)
- [Tips for Best Results](#tips-for-best-results)
- [Understanding the Output](#understanding-the-output)
- [Troubleshooting](#troubleshooting)

---

## Quick Start

Once installed, the plugin works **automatically** — no buttons to click, no windows to open. Just talk to your AI assistant:

```
"Start a trace, let me play for 30 seconds, then analyze the performance"
```

The AI handles everything via MCP tools behind the scenes.

---

## How It Works in the Editor

### What Happens When You Open the Editor

1. UE loads the `UEInsightMCP` plugin module
2. The `UInsightBridge` (an EditorSubsystem) initializes automatically
3. A TCP server starts listening on **port 55559** (localhost only)
4. 20 action handlers are registered and ready to receive commands
5. You see this in the Output Log:

```
LogInsightMCP: UEInsightMCP: Server started on port 55559 (max 8 clients)
LogInsightMCP: UEInsightMCP: Registered 20 action handlers
```

### What You DON'T Need to Do

- ❌ No need to open any special window or panel
- ❌ No need to enable the plugin via UI toggle (it auto-enables)
- ❌ No need to start any service manually
- ❌ No need to run the Python bridge yourself (your MCP client starts it)

### What Runs Where

| Action type | Thread | Why |
|-------------|--------|-----|
| `trace.*` (start/stop/channels/bookmark) | **Game Thread** | Must interact with engine Trace API |
| `session.*` (list/open/info/close) | **TCP Worker Thread** | Reads .utrace files, no engine state needed |
| `query.*` (all 9 queries) | **TCP Worker Thread** | Pure data analysis, doesn't touch the editor |

**This means**: Running heavy queries (e.g., parsing a 500MB trace) will NOT freeze your editor. You can keep working while analysis runs.

---

## Workflow 1: Live Performance Profiling

**Scenario**: You want to profile a specific gameplay moment.

### Step by Step

```
Step 1: Enable channels and start recording
─────────────────────────────────────────────
AI → trace.channels.toggle(channels: "cpu,gpu,frame,loadtime,bookmark", enable: true)
AI → trace.start(channels: "cpu,gpu,frame,loadtime,bookmark", output: "file")

Step 2: Perform the action you want to profile
─────────────────────────────────────────────
You → Play in Editor (PIE), load a level, spawn actors, etc.
AI → trace.bookmark.add(label: "entered_combat")        ← optional: mark key moments
     ... keep playing ...
AI → trace.bookmark.add(label: "boss_defeated")

Step 3: Stop recording
─────────────────────────────────────────────
AI → trace.stop
     Returns: { file: "C:/.../Saved/TraceSessions/Trace_20260330_112000.utrace", size_mb: 45.2 }

Step 4: Open for analysis
─────────────────────────────────────────────
AI → session.open(file: "<path from step 3>")
AI → session.info                             ← check what data is available

Step 5: Query what you need
─────────────────────────────────────────────
AI → query.frame_times                        ← overall FPS stats
AI → query.hitches(threshold_ms: 33.33)       ← find stutters
AI → query.cpu_threads(top_n: 10)             ← hot threads & functions
AI → query.bookmarks                          ← verify your markers landed

Step 6: Clean up
─────────────────────────────────────────────
AI → session.close
```

### Example Conversation

```
You:  "Profile the PIE startup for me"
AI:   I'll set up channels and start recording. Go ahead and press Play.

      [trace.channels.toggle → trace.start]

You:  "OK it's loaded, I'm in the main menu now"
AI:   [trace.bookmark.add("main_menu_reached") → trace.stop]
      Recording saved (38.7 MB). Let me analyze...

      [session.open → query.frame_times → query.loadtime → query.hitches]

      Results:
      - 1,247 frames over 22.3s (avg 55.9 FPS)
      - 47 hitches > 33ms (mostly during initial asset loading)
      - Top bottleneck: /Game/Maps/MainMenu loads 89 packages in 12.1s
      - Heaviest asset: ControlRig_Mannequin (153ms, 4,283 exports)
```

---

## Workflow 2: Offline Trace Analysis

**Scenario**: You already have `.utrace` files (from CI, another machine, or previous sessions).

```
Step 1: List available traces
─────────────────────────────────────────────
AI → session.list(limit: 10)
     Returns newest .utrace files in {Project}/Saved/TraceSessions/

Step 2: Open a specific trace
─────────────────────────────────────────────
AI → session.open(file: "C:/Traces/build_1234.utrace")
AI → session.info
     Shows: 15,000 frames, 24 threads, timing + loadtime + memory providers available

Step 3: Run queries
─────────────────────────────────────────────
AI → query.frame_times(frame_type: "game")
AI → query.cpu_threads(filter_name: "GameThread", top_n: 20)
AI → query.loadtime(sort_by: "duration", top_n: 50)
AI → query.loadtime_deps(package: "/Game/Maps/")
AI → query.memory(tag: "Texture")
AI → query.counters(filter: "DrawCalls", include_values: true)

Step 4: Close
─────────────────────────────────────────────
AI → session.close
```

### Using a Custom Directory

By default, `session.list` searches `{Project}/Saved/TraceSessions/`. To search elsewhere:

```
AI → session.list(directory: "D:/CI/TraceOutputs/", limit: 20)
```

---

## Workflow 3: Asset Loading Analysis

**Scenario**: Startup is slow, you need to understand the loading timeline.

> **Critical**: For complete load-time data, the `loadtime` channel MUST be enabled **before** recording starts. If you're analyzing engine startup, use command-line arguments (see [Tips](#tips-for-best-results)).

```
AI → session.open(file: "<your trace>")

# Timeline view: loading requests in chronological order
AI → query.loadtime(sort_by: "start_time", top_n: 100)

# What's slowest
AI → query.loadtime(sort_by: "duration", top_n: 20)

# Dependency graph: why was this package loaded?
AI → query.loadtime_deps(package: "/Game/Characters/Hero")

# Memory impact
AI → query.memory(tag: "Texture")

AI → session.close
```

### What the AI Can Tell You

Based on `query.loadtime` + `query.loadtime_deps` data, the AI can:
- Identify **loading phases** (engine init → config → map → gameplay assets)
- Find **serial bottlenecks** (single packages that block the critical path)
- Detect **unexpected dependencies** (why is Audio loading during a menu?)
- Calculate **per-phase timing** and parallelism efficiency
- Suggest **optimization strategies** (async loading, preloading, trimming deps)

---

## Workflow 4: Quick Health Check

**Scenario**: Is everything working? Just a quick ping.

```
# Single command check
AI → insight_batch([
  {action_id: "trace.status"},
  {action_id: "trace.channels.list"}
])

# Returns current trace state + all available channels
```

Or just:
```
You:  "Check if Insight MCP is working"
AI:   [insight_ping → trace.status → trace.channels.list]
      ✅ Connected. Not currently tracing. 47 channels available.
```

---

## Action Parameter Reference

### trace.start

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `channels` | string | `cpu,gpu,frame,memory,loadtime` | Comma-separated channel names |
| `output` | string | `file` | `file` (local .utrace) or `server` (stream to Trace Server) |
| `host` | string | `127.0.0.1` | Trace Server address (server mode only) |

### trace.channels.toggle

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `channels` | string | *(required)* | Comma-separated channel names to toggle |
| `enable` | boolean | `true` | `true` to enable, `false` to disable |

### trace.bookmark.add

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `label` | string | *(required)* | Bookmark text (appears in `query.bookmarks`) |

### session.list

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `directory` | string | `{Project}/Saved/TraceSessions/` | Directory to search |
| `limit` | integer | `20` | Max files to return (newest first) |

### session.open

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `file` | string | *(required)* | Full path to `.utrace` file |

### query.frame_times

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `frame_type` | string | `game` | `game` or `render` |
| `limit` | integer | `10` | Number of worst frames to return |

### query.cpu_threads

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `top_n` | integer | `20` | Top timers per thread |
| `filter_name` | string | — | Filter threads by name (partial match) |

### query.gpu_timing

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `top_n` | integer | `20` | Top GPU timers to return |

### query.loadtime

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `top_n` | integer | `30` | Max requests to return |
| `sort_by` | string | `duration` | `duration` (slowest first) or `start_time` (chronological) |

### query.loadtime_deps

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `package` | string | — | Filter packages by name (partial match) |
| `top_n` | integer | `20` | Max packages to return |

### query.memory

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `tag` | string | — | Filter memory tags by name (partial match) |
| `tracker_id` | integer | `0` | Tracker ID (0 = default tracker) |

### query.counters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `filter` | string | — | Filter counters by name (partial match) |
| `group` | string | — | Filter by group (partial match) |
| `include_values` | boolean | `false` | Include sample values |
| `max_samples` | integer | `100` | Max samples per counter |

### query.bookmarks

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `filter` | string | — | Filter bookmarks by text (partial match) |

### query.hitches

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `threshold_ms` | number | `33.33` | Frame duration threshold in ms |
| `limit` | integer | `50` | Max hitches to return |
| `frame_type` | string | `game` | `game` or `render` |

---

## Tips for Best Results

### 1. Enable Channels BEFORE Recording

Channels must be enabled before `trace.start` for complete data capture. The recommended sequence:

```
trace.channels.toggle(channels: "cpu,gpu,frame,loadtime,bookmark,memory", enable: true)
trace.start(channels: "cpu,gpu,frame,loadtime,bookmark,memory")
```

### 2. For Startup Profiling, Use Command-Line Arguments

If you need to capture data from engine startup (before the plugin initializes), add to your engine launch arguments:

```
-trace=cpu,frame,log,bookmark,loadtime,assetloadtime,file
```

This starts tracing at engine boot, before any plugin or game code runs.

### 3. Use Bookmarks to Mark Phases

Insert bookmarks at key moments to make analysis easier:

```
trace.bookmark.add(label: "level_streaming_start")
trace.bookmark.add(label: "all_players_spawned")
trace.bookmark.add(label: "round_1_begin")
```

The AI can use these to segment the timeline and calculate per-phase metrics.

### 4. Sort Loadtime by start_time for Timeline View

```
query.loadtime(sort_by: "start_time", top_n: 100)
```

This gives a chronological view — the AI can reconstruct what loaded when and identify serial bottlenecks.

### 5. Combine Queries for Cross-Analysis

The real power is combining multiple queries in one conversation:

```
query.frame_times      → "average FPS is 45, but there are 23 hitches"
query.hitches          → "hitches cluster between frames 1200-1500"
query.cpu_threads      → "GameThread spikes during those frames: GC + streaming"
query.loadtime         → "300MB texture streaming request at frame 1200"
```

The AI connects the dots automatically.

### 6. Large Traces: Be Patient with session.open

Opening a 500MB+ trace file may take 10-30 seconds for `session.open` to parse all data. The progress is logged in UE's Output Log. Subsequent queries are fast (they read the already-parsed in-memory data).

### 7. Always Close Sessions

After analysis, call `session.close` to free memory. An open session holds all parsed trace data in RAM.

---

## Understanding the Output

### query.frame_times Output

```json
{
  "summary": {
    "total_frames": 1247,
    "avg_ms": 17.89,      // Average frame time
    "avg_fps": 55.9,
    "min_ms": 8.2,
    "max_ms": 312.5,
    "p95_ms": 28.4,       // 95th percentile
    "p99_ms": 67.1        // 99th percentile — if this is far from p95, you have outlier hitches
  },
  "distribution": [        // FPS buckets
    {"label": "120+ fps", "count": 0, "percentage": 0.0},
    {"label": "90-120",   "count": 45, "percentage": 3.6},
    {"label": "60-90",    "count": 812, "percentage": 65.1},
    {"label": "30-60",    "count": 343, "percentage": 27.5},
    {"label": "<30 fps",  "count": 47, "percentage": 3.8}
  ],
  "worst_frames": [        // The worst individual frames
    {"frame_index": 1234, "duration_ms": 312.5, "fps": 3.2},
    ...
  ]
}
```

**Key insight**: If `p99` is much higher than `p95`, you have occasional severe hitches but generally stable performance.

### query.hitches Output

```json
{
  "hitch_count": 47,
  "total_frames": 1247,
  "hitch_percentage": 3.8,
  "summary": {
    "worst_hitch_ms": 312.5,
    "avg_hitch_ms": 58.7,
    "total_hitch_time_ms": 2758.9
  },
  "hitches": [
    {"frame_index": 1234, "duration_ms": 312.5, "severity": "critical"},
    ...
  ]
}
```

**Severity levels** (by convention):
- `> 100ms` — critical (visible freeze)
- `> 50ms` — major (noticeable stutter)
- `> 33.33ms` — minor (frame drop below 30fps)

### query.loadtime Output

```json
{
  "total_assets": 472,
  "total_time_ms": 21340.5,
  "assets": [
    {
      "name": "/Game/Maps/MainMenu",
      "load_time_ms": 4523.1,
      "package_count": 89,
      "export_count": 12345
    },
    ...
  ]
}
```

---

## Troubleshooting

### "Trace is already running"

Call `trace.stop` first, then `trace.start`. Use `trace.status` to check current state.

### "No session open"

You must call `session.open` with a `.utrace` file path before running any `query.*` action. Use `session.list` to find available trace files.

### "Not connected to UE Insight"

1. Confirm UEInsightMCP plugin is enabled in UE Editor
2. Confirm port 55559 is not occupied (`netstat -ano | findstr 55559`)
3. Test with `insight_ping`

### LoadTimeProfiler returns no data

- Ensure `loadtime` channel is enabled **before** trace recording starts
- For startup profiling, use engine command-line: `-trace=cpu,frame,log,bookmark,loadtime,assetloadtime,file`
- Or toggle channels at runtime before recording:
  ```
  trace.channels.toggle(channels: "loadtime,assetloadtime,file", enable: true)
  ```
- The plugin automatically activates `LoadTimeProfilerModule` when creating analysis sessions (fixes the UE5 `WITH_EDITOR` default-disabled issue)

### Query actions timeout

- Large trace files (>100MB) may take longer to parse during `session.open`
- Once opened, queries should be fast (in-memory data)
- Query actions run on TCP worker threads and won't block the editor

### Session opens but providers are missing

After `session.open`, run `session.info` to see which providers are available:

```json
{
  "has_timing_profiler": true,
  "has_loadtime_profiler": true,   // false = loadtime channel wasn't recorded
  "has_counter_provider": true,
  "has_bookmark_provider": true
}
```

If a provider is `false`, the corresponding channel wasn't enabled during recording. You'll need to re-record with the right channels.

### Memory usage is high

Each open analysis session holds all parsed trace data in RAM. A 100MB `.utrace` file might expand to 500MB+ in memory. Always `session.close` when done.

---

## Built-in TCP Commands

These are low-level commands handled directly by the TCP server (not action handlers):

| Command | Description |
|---------|-------------|
| `ping` | Heartbeat, returns `{pong: true}` |
| `close` | Close the TCP connection |
| `get_context` | Get current InsightContext state as JSON |

### Log Capture

The plugin captures UE editor logs in a ring buffer:

| Parameter | Value |
|-----------|-------|
| Capacity | 10,000 entries / 5 MB |
| Max message length | 8,192 chars (truncated) |
| Thread-safe | Yes |
| Filterable by | Category, severity, keyword |

Access via `insight_logs_tail` MCP tool:

```
insight_logs_tail(source: "editor", lines: 50, category: "LogInsightMCP", severity: "warning")
```

---

*See [README.md](README.md) for architecture overview and action list.*
*See [INSTALL.md](INSTALL.md) for installation instructions.*
