# UE Insight MCP — Error Codes

## Error Types

| Error Type | Description | Recovery |
|---|---|---|
| `validation_failed` | Input parameters didn't pass validation | Fix the parameters |
| `trace_start_failed` | FTraceAuxiliary::Start returned false | Check if Trace is already running |
| `unknown_command` | Action ID not registered | Use insight_actions_search |
| `crash_prevented` | SEH caught an access violation | Report as bug |
| `cpp_exception` | C++ exception caught | Check UE logs |
| `connection_error` | TCP connection to UE lost | Wait for UE or restart |
| `timeout` | Game thread timed out (>240s) | UE might be hanging |

## Common Issues

### "Trace is already running"
- Call `trace.stop` first, then `trace.start`
- Use `trace.status` to check current state

### "Not connected to UE Insight"
- Ensure UEInsightMCP plugin is enabled in UE Editor
- Check that port 55559 is not blocked
- Try `insight_ping` to test connectivity

### "Unknown command type"
- The action is not registered in C++ InsightBridge
- Likely a Phase 2/3 action that hasn't been implemented yet
- Use `insight_actions_search` to find available actions
