# UE Insight MCP — Conventions

## Action Discovery Pattern

Always follow this 3-step pattern:

1. **Search**: `insight_actions_search` with keywords → discover action IDs
2. **Schema**: `insight_actions_schema` with action ID → learn parameters
3. **Run**: `insight_actions_run` with action ID + params → execute

## Trace Workflow

### Basic Profile Session
```
1. trace.start (channels: cpu,gpu,frame,memory)
2. ... perform the workload to profile ...
3. trace.bookmark.add (label: "workload_complete")
4. trace.stop → returns .utrace file path
5. session.open (file: <path from step 4>)
6. query.frame_times → analyze frame performance
7. analyze.bottleneck → identify issues
```

### Quick Status Check
```
1. trace.status → see if Trace is running, which channels, elapsed time
2. trace.channels.list → see all available channels
```

## Naming Conventions

- Action IDs use dot notation: `category.action` (e.g., `trace.start`)
- Channel names are lowercase: `cpu`, `gpu`, `frame`, `memory`, `loadtime`
- File paths use forward slashes even on Windows
- Timestamps are in seconds (FPlatformTime::Seconds)

## Error Handling

- All actions return `{success: true/false, ...}`
- Errors include `error` (message) and `error_type` (category)
- Crash protection via SEH prevents editor crashes from bad actions
- Network timeouts are 240 seconds for game-thread operations

## Ports

- UEEditorMCP: TCP 55558
- UEInsightMCP: TCP 55559
- Both run independently in the same UE Editor process
