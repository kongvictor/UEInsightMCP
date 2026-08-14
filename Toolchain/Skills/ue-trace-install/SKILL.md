---
name: ue-trace-install
description: >
  Install, repair, verify, or upgrade the full UEInsightMCP trace toolchain on Windows — the
  live Editor bridge (ue-insight-mcp), the patched offline analyzer (ue-trace), the native
  TraceDigest runtime, the Claude Code MCP registrations, and the bundled skills. Use when the
  user asks to set up trace tooling on a new machine or project, when insight_ping or a
  trace_* tool fails because an MCP is missing or broken, or when pinning a new
  @mtuska/ue-trace-mcp version. Trigger phrases include "安装 trace 工具链", "配置 UEInsightMCP",
  "trace MCP 装不上", "insight_ping 失败", "ue-trace 用不了", "重装 trace 分析器",
  "install trace toolchain", and "setup UE insight MCP".
version: 1.0.0
---

# UEInsightMCP Toolchain Installation

## What gets installed

The toolchain has two independent halves. Both are required for the full capture-and-analyze
workflow, but each is useful alone, so install and verify them separately.

| Component | What it is | Where it lands |
|---|---|---|
| `ue-insight-mcp` | Live Editor/PIE bridge (Python). Talks to the plugin over TCP `127.0.0.1:55559`. | `%USERPROFILE%\.local\bin\ue-insight-mcp.exe` |
| Plugin binary | `UnrealEditor-UEInsightMCP.dll` — hosts the TCP server inside the Editor. | `<Plugin>\Binaries\Win64\` |
| `ue-trace` | Offline `.utrace` analyzer (`@mtuska/ue-trace-mcp`), patched for Windows. | `%USERPROFILE%\.local\share\ue-trace-mcp\<version>\app\` |
| TraceDigest runtime | Native analysis binary plus `tbbmalloc.dll` and `WinPixEventRuntime.dll`. | `...\<version>\native\windows-x64\` |
| Skills | `ue-trace` (analysis) and `ue-trace-install` (this file). | `%USERPROFILE%\.claude\skills\` |

Everything except the plugin binary is handled by `Toolchain\Install-Toolchain.ps1`.

## Step 1 — Audit before changing anything

Always run the check pass first, including on a machine the user believes is already set up.
It is read-only and names the exact component that is broken:

~~~powershell
& "<PluginDir>\Toolchain\Install-Toolchain.ps1" -CheckOnly
~~~

Report which components are missing before installing. Do not reinstall a healthy component
just because a different one failed.

## Step 2 — Run the installer

~~~powershell
& "<PluginDir>\Toolchain\Install-Toolchain.ps1"
~~~

It is idempotent — re-running repairs a partial install rather than duplicating one. Useful
flags:

- `-Force` — reinstall the npm package and re-download the native runtime even if present.
- `-SkipMcpRegister` — leave `~/.claude.json` untouched (use when the user manages MCP config
  by hand, or when the registration step is blocked).
- `-SkipSkills`, `-SkipPythonBridge`, `-SkipTraceAnalyzer` — install one half only.
- `-TraceMcpVersion <v>` — pin a different analyzer version. A matching patch must exist in
  `Toolchain\Patches\`; see "Upgrading" below.

The installer verifies the downloaded release archive against the SHA-256 baked into the npm
package's `binaries.json`. If that check fails it refuses to extract — treat a mismatch as a
real integrity problem, not something to bypass.

Prerequisites it will report on rather than install: Node.js 18+, npm, git, `uv`, and the
`claude` CLI.

## Step 3 — Build the plugin binary

The Editor loads `UnrealEditor-UEInsightMCP.dll` from `<Plugin>\Binaries\Win64\`. The DLL's
BuildId must match the engine, or the Editor silently skips the plugin.

Normal path — build the editor target from the engine root:

~~~text
Engine\Build\BatchFiles\Build.bat <Project>Editor Win64 Development -Project="<abs .uproject>" -WaitMutex -NoHotReloadFromIDE
~~~

If that fails with UBA detour errors (`DetourCreateProcessWithDllEx failed ... Exit code: 740 -
The requested operation requires elevation`), do not retry in a loop and do not attempt to
elevate. Package the plugin standalone instead, then copy the artifacts back:

~~~text
Engine\Build\BatchFiles\RunUAT.bat BuildPlugin -Plugin="<abs .uplugin>" -Package="<temp dir>" -TargetPlatforms=Win64
~~~

Then copy `UnrealEditor-UEInsightMCP.dll`, its `.pdb`, and `UnrealEditor.modules` from
`<temp dir>\Binaries\Win64\` into `<Plugin>\Binaries\Win64\`. Confirm the `BuildId` in
`UnrealEditor.modules` matches the engine's before trusting the result.

Both build paths fail while the Editor or an IDE backend holds the DLL open. Close the Editor
first. Never force-kill a user's Editor or IDE process to unblock a build — report the block
and let the user close it.

## Step 4 — Enable the plugin and launch the Editor

Add the plugin to the `.uproject`:

~~~json
{ "Name": "UEInsightMCP", "Enabled": true }
~~~

Editing a `.uproject` that is checked out in a user's source-control changelist is a change to
their pending work — confirm before touching it.

Launch with trace enabled. Startup-only channels must be requested here; they cannot be turned
on later:

~~~text
UnrealEditor.exe "<abs .uproject>" -trace=default,cpu -tracehost=localhost -enabletraceserver
~~~

Add `,memory` to `-trace` when allocation data is needed (`memtag`, `memalloc`, `callstack`,
`module`). That makes captures much larger — use short windows.

On PowerShell, quote any argument containing a comma, or the parser splits it:
`"-trace=default,cpu,memory"`.

## Step 5 — Verify end to end

Restart Claude Code first so it picks up the MCP registrations, then confirm in this order and
stop at the first failure:

1. `claude mcp list` shows both `ue-insight-mcp` and `ue-trace` as connected.
2. `insight_ping` succeeds — proves the plugin loaded and the TCP server is listening.
3. `trace.channels.list` returns channels with `startup_only` / `runtime_toggleable` fields.
4. A short capture round-trip: `trace.start` (`output=file`, `replace_existing=true`), a
   bookmark, `trace.stop` (`restore_previous=true`). Check that the returned path is absolute,
   `file_exists=true`, and the size has settled.
5. `trace_channels` and `trace_overview` on that file — proves the offline half works on a
   trace the live half produced.

Report which of these actually ran. A passing `claude mcp list` is not evidence that capture
works.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `insight_ping` fails, Editor running | Plugin DLL missing or BuildId mismatch | Redo step 3; check the Editor log for `LogInsightMCP` |
| `insight_ping` fails, no Editor | Nothing listening on 55559 | Start the Editor with the plugin enabled |
| Port 55559 in use | Another Editor instance | `netstat -ano \| findstr 55559`, close the other instance |
| Offline tool: missing-runtime error | `tbbmalloc.dll` / `WinPixEventRuntime.dll` not beside `TraceDigest.exe` | `Install-Toolchain.ps1 -Force` |
| Offline analysis times out on a large trace | Default one-shot timeout too low | Raise `TRACE_ONESHOT_TIMEOUT_MS` (ms) in the `ue-trace` MCP env |
| Analyzer hangs or spawns a stray daemon | Unpatched dist — the daemon path is POSIX-only | `Install-Toolchain.ps1 -CheckOnly`; reinstall if fixes are absent |
| `claude mcp add` reports "Invalid environment variable format" | `-e` is variadic and swallowed the server name | Put the server name *before* the first `-e` |
| Trace files land in a duplicated project directory | Plugin predates the path-normalization fix | Rebuild from a source tree that contains it |

## Upgrading the analyzer

The Windows fixes live in `Toolchain\Patches\ue-trace-mcp-<version>-windows.patch` and are
version-specific. To move to a new upstream version:

1. Install the new version unpatched and confirm which fixes upstream has adopted.
2. Re-apply the still-needed changes and regenerate the patch by diffing the pristine package
   against the patched tree, with paths relative to the package root (`a/dist/...`,
   `b/dist/...`).
3. Verify with `git apply --check` against a clean extract, then `node --check` each patched
   file.
4. Commit the new patch alongside the old one and run
   `Install-Toolchain.ps1 -TraceMcpVersion <new>`.

The four fixes the patch carries — disable the POSIX daemon on win32, route one-shot output
through a unique `-out=<json>` file, raise the win32 default timeout to 300s with a
`TRACE_ONESHOT_TIMEOUT_MS` override, and assert the three native runtime files are present —
are each independently droppable if upstream fixes them.

## Boundaries

Never delete `.utrace` files as part of install, repair, or cleanup. Never force-kill Editor or
IDE processes. Treat writes to `~/.claude.json`, the `.uproject`, and any source-controlled file
as changes to the user's environment: say what will change before doing it, and never commit or
submit to source control unless the user asks.
