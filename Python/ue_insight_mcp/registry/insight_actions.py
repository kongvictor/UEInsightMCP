"""
Insight Action Definitions — All Trace/Insight action metadata.

Phase 1: Trace Control (6 actions)
Phase 2: Session Management (4 actions) + Data Query (9 actions)
"""

from . import ActionDef, ActionRegistry


def register_all_actions(registry: ActionRegistry) -> None:
    """Register all Insight action definitions."""
    registry.register_many(TRACE_CONTROL_ACTIONS)
    registry.register_many(SESSION_ACTIONS)
    registry.register_many(QUERY_ACTIONS)
    # Phase 3: registry.register_many(ANALYZE_ACTIONS)
    # Phase 3: registry.register_many(REPORT_ACTIONS)


# ============================================================================
# Phase 1: Trace Control Actions
# ============================================================================

TRACE_CONTROL_ACTIONS = [
    ActionDef(
        id="trace.start",
        command="trace.start",
        tags=("trace", "start", "record", "profile", "capture", "begin"),
        description="Start Trace recording with specified channels and output mode",
        input_schema={
            "type": "object",
            "properties": {
                "channels": {
                    "type": "string",
                    "description": "Comma-separated channel names. Default: cpu,gpu,frame,memory,loadtime",
                    "default": "cpu,gpu,frame,memory,loadtime",
                },
                "output": {
                    "type": "string",
                    "enum": ["file", "server"],
                    "description": "Output mode: 'file' saves .utrace locally, 'server' streams to Trace Server",
                    "default": "file",
                },
                "host": {
                    "type": "string",
                    "description": "Trace Server host (only used when output='server')",
                    "default": "127.0.0.1",
                },
            },
        },
        examples=(
            {"channels": "cpu,gpu,frame", "output": "file"},
            {"channels": "cpu,gpu,frame,memory,loadtime,bookmark", "output": "file"},
            {"output": "server", "host": "127.0.0.1"},
        ),
        capabilities=("write",),
        risk="safe",
    ),

    ActionDef(
        id="trace.stop",
        command="trace.stop",
        tags=("trace", "stop", "end", "finish", "record"),
        description="Stop the current Trace recording and return file info",
        input_schema={
            "type": "object",
            "properties": {},
        },
        examples=(
            {},
        ),
        capabilities=("write",),
        risk="safe",
    ),

    ActionDef(
        id="trace.status",
        command="trace.status",
        tags=("trace", "status", "info", "state", "check"),
        description="Get current Trace status (running, channels, file, elapsed time)",
        input_schema={
            "type": "object",
            "properties": {},
        },
        examples=(
            {},
        ),
        capabilities=("read",),
        risk="safe",
    ),

    ActionDef(
        id="trace.channels.list",
        command="trace.channels.list",
        tags=("trace", "channels", "list", "enumerate", "available"),
        description="List all registered Trace channels and their enabled/disabled state",
        input_schema={
            "type": "object",
            "properties": {},
        },
        examples=(
            {},
        ),
        capabilities=("read",),
        risk="safe",
    ),

    ActionDef(
        id="trace.channels.toggle",
        command="trace.channels.toggle",
        tags=("trace", "channels", "toggle", "enable", "disable", "switch"),
        description="Enable or disable specific Trace channels",
        input_schema={
            "type": "object",
            "properties": {
                "channels": {
                    "type": "string",
                    "description": "Comma-separated channel names to toggle",
                },
                "enable": {
                    "type": "boolean",
                    "description": "True to enable, False to disable",
                    "default": True,
                },
            },
            "required": ["channels"],
        },
        examples=(
            {"channels": "memory,loadtime", "enable": True},
            {"channels": "gpu", "enable": False},
        ),
        capabilities=("write",),
        risk="safe",
    ),

    ActionDef(
        id="trace.bookmark.add",
        command="trace.bookmark.add",
        tags=("trace", "bookmark", "mark", "label", "tag", "annotation"),
        description="Add a named bookmark at the current time in the Trace stream",
        input_schema={
            "type": "object",
            "properties": {
                "label": {
                    "type": "string",
                    "description": "Bookmark label text",
                },
            },
            "required": ["label"],
        },
        examples=(
            {"label": "before_optimization"},
            {"label": "level_loaded"},
            {"label": "boss_spawned"},
        ),
        capabilities=("write",),
        risk="safe",
    ),
]


# ============================================================================
# Phase 2: Session Management Actions
# ============================================================================

SESSION_ACTIONS = [
    ActionDef(
        id="session.list",
        command="session.list",
        tags=("session", "list", "files", "traces", "utrace", "browse"),
        description="List available .utrace files in the default trace directory or a custom path",
        input_schema={
            "type": "object",
            "properties": {
                "directory": {
                    "type": "string",
                    "description": "Directory to search for .utrace files. Defaults to {ProjectDir}/Saved/TraceSessions/",
                },
                "limit": {
                    "type": "integer",
                    "description": "Maximum number of files to return (newest first)",
                    "default": 20,
                },
            },
        },
        examples=(
            {},
            {"limit": 10},
            {"directory": "C:/MyTraces"},
        ),
        capabilities=("read",),
        risk="safe",
    ),

    ActionDef(
        id="session.open",
        command="session.open",
        tags=("session", "open", "load", "analyze", "utrace", "file"),
        description="Open a .utrace file for analysis. This parses all trace data and makes it available for query.* actions",
        input_schema={
            "type": "object",
            "properties": {
                "file": {
                    "type": "string",
                    "description": "Full path to the .utrace file to analyze",
                },
            },
            "required": ["file"],
        },
        examples=(
            {"file": "C:/Project/Saved/TraceSessions/Trace_20260326_120000.utrace"},
        ),
        capabilities=("read",),
        risk="safe",
    ),

    ActionDef(
        id="session.info",
        command="session.info",
        tags=("session", "info", "details", "status", "providers"),
        description="Get detailed info about the currently loaded analysis session (frame count, threads, available providers)",
        input_schema={
            "type": "object",
            "properties": {},
        },
        examples=(
            {},
        ),
        capabilities=("read",),
        risk="safe",
    ),

    ActionDef(
        id="session.close",
        command="session.close",
        tags=("session", "close", "unload", "release"),
        description="Close the current analysis session and free resources",
        input_schema={
            "type": "object",
            "properties": {},
        },
        examples=(
            {},
        ),
        capabilities=("write",),
        risk="safe",
    ),
]


# ============================================================================
# Phase 2: Data Query Actions
# ============================================================================

QUERY_ACTIONS = [
    ActionDef(
        id="query.frame_times",
        command="query.frame_times",
        tags=("query", "frame", "fps", "framerate", "performance", "time", "statistics"),
        description="Get frame time statistics: avg/min/max/median/p95/p99, FPS distribution, and worst frames",
        input_schema={
            "type": "object",
            "properties": {
                "frame_type": {
                    "type": "string",
                    "enum": ["game", "render"],
                    "description": "Frame type to analyze",
                    "default": "game",
                },
                "limit": {
                    "type": "integer",
                    "description": "Number of worst frames to return (default 10)",
                    "default": 10,
                },
            },
        },
        examples=(
            {},
            {"frame_type": "game"},
            {"frame_type": "render", "limit": 20},
        ),
        capabilities=("read",),
        risk="safe",
    ),

    ActionDef(
        id="query.cpu_threads",
        command="query.cpu_threads",
        tags=("query", "cpu", "threads", "timing", "profiler", "aggregation"),
        description="Get CPU thread timing summary with top timers per thread",
        input_schema={
            "type": "object",
            "properties": {
                "top_n": {
                    "type": "integer",
                    "description": "Number of top timers to return per thread",
                    "default": 20,
                },
                "filter_name": {
                    "type": "string",
                    "description": "Filter threads by name (partial match)",
                },
            },
        },
        examples=(
            {},
            {"top_n": 10},
            {"filter_name": "GameThread"},
        ),
        capabilities=("read",),
        risk="safe",
    ),

    ActionDef(
        id="query.gpu_timing",
        command="query.gpu_timing",
        tags=("query", "gpu", "timing", "render", "graphics", "profiler"),
        description="Get GPU timing data with top GPU timers by total time",
        input_schema={
            "type": "object",
            "properties": {
                "top_n": {
                    "type": "integer",
                    "description": "Number of top GPU timers to return",
                    "default": 20,
                },
            },
        },
        examples=(
            {},
            {"top_n": 30},
        ),
        capabilities=("read",),
        risk="safe",
    ),

    ActionDef(
        id="query.loadtime",
        command="query.loadtime",
        tags=("query", "loadtime", "loading", "assets", "packages", "startup"),
        description="Get asset loading time data: load requests sorted by duration or start time",
        input_schema={
            "type": "object",
            "properties": {
                "top_n": {
                    "type": "integer",
                    "description": "Number of top requests to return",
                    "default": 30,
                },
                "sort_by": {
                    "type": "string",
                    "enum": ["duration", "start_time"],
                    "description": "Sort order for results",
                    "default": "duration",
                },
            },
        },
        examples=(
            {},
            {"sort_by": "start_time", "top_n": 50},
        ),
        capabilities=("read",),
        risk="safe",
    ),

    ActionDef(
        id="query.loadtime_deps",
        command="query.loadtime_deps",
        tags=("query", "loadtime", "dependencies", "packages", "imports", "graph"),
        description="Get asset loading dependency graph: packages with their dependencies and timing",
        input_schema={
            "type": "object",
            "properties": {
                "package": {
                    "type": "string",
                    "description": "Filter packages by name (partial match)",
                },
                "top_n": {
                    "type": "integer",
                    "description": "Number of top packages to return",
                    "default": 20,
                },
            },
        },
        examples=(
            {},
            {"package": "/Game/Maps/"},
            {"package": "Engine", "top_n": 50},
        ),
        capabilities=("read",),
        risk="safe",
    ),

    ActionDef(
        id="query.memory",
        command="query.memory",
        tags=("query", "memory", "llm", "tags", "allocation", "usage"),
        description="Get memory (LLM) tag data: trackers, tags with latest values",
        input_schema={
            "type": "object",
            "properties": {
                "tag": {
                    "type": "string",
                    "description": "Filter tags by name (partial match)",
                },
                "tracker_id": {
                    "type": "integer",
                    "description": "Tracker ID to query (default 0 = default tracker)",
                    "default": 0,
                },
            },
        },
        examples=(
            {},
            {"tag": "Texture"},
            {"tag": "Audio", "tracker_id": 0},
        ),
        capabilities=("read",),
        risk="safe",
    ),

    ActionDef(
        id="query.counters",
        command="query.counters",
        tags=("query", "counters", "stats", "metrics", "named"),
        description="Get named counter data: list counters with optional value samples",
        input_schema={
            "type": "object",
            "properties": {
                "filter": {
                    "type": "string",
                    "description": "Filter counters by name (partial match)",
                },
                "group": {
                    "type": "string",
                    "description": "Filter counters by group (partial match)",
                },
                "include_values": {
                    "type": "boolean",
                    "description": "Include sample values for each counter",
                    "default": False,
                },
                "max_samples": {
                    "type": "integer",
                    "description": "Max samples per counter when include_values=true",
                    "default": 100,
                },
            },
        },
        examples=(
            {},
            {"filter": "DrawCalls"},
            {"group": "Rendering", "include_values": True, "max_samples": 50},
        ),
        capabilities=("read",),
        risk="safe",
    ),

    ActionDef(
        id="query.bookmarks",
        command="query.bookmarks",
        tags=("query", "bookmarks", "marks", "labels", "annotations"),
        description="Get all bookmarks (TRACE_BOOKMARK) from the trace session",
        input_schema={
            "type": "object",
            "properties": {
                "filter": {
                    "type": "string",
                    "description": "Filter bookmarks by text (partial match)",
                },
            },
        },
        examples=(
            {},
            {"filter": "level"},
        ),
        capabilities=("read",),
        risk="safe",
    ),

    ActionDef(
        id="query.hitches",
        command="query.hitches",
        tags=("query", "hitches", "stutter", "jank", "lag", "spike", "freeze"),
        description="Detect hitches: frames exceeding a duration threshold, sorted by severity",
        input_schema={
            "type": "object",
            "properties": {
                "threshold_ms": {
                    "type": "number",
                    "description": "Hitch threshold in milliseconds. Frames longer than this are reported.",
                    "default": 33.33,
                },
                "limit": {
                    "type": "integer",
                    "description": "Max hitches to return",
                    "default": 50,
                },
                "frame_type": {
                    "type": "string",
                    "enum": ["game", "render"],
                    "description": "Frame type to check",
                    "default": "game",
                },
            },
        },
        examples=(
            {},
            {"threshold_ms": 16.67},
            {"threshold_ms": 50, "limit": 100, "frame_type": "render"},
        ),
        capabilities=("read",),
        risk="safe",
    ),
]
