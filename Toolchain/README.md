# Toolchain

Everything needed to go from a clean Windows machine to a working
capture-and-analyze loop for Unreal Engine `.utrace` files.

The plugin in `Source/` is only half the story. It exposes trace control inside
the Editor, but analysis happens offline in a separate process against a
separate binary. This directory installs and wires up both halves.

## Layout

| Path | What it is |
|---|---|
| `Install-Toolchain.ps1` | Idempotent Windows installer for both MCP servers, the native runtime, and the skills. |
| `Patches/` | Version-pinned patches that make `@mtuska/ue-trace-mcp` work on Windows. |
| `Skills/ue-trace/` | Claude Code skill for capturing and analyzing traces. |
| `Skills/ue-trace-install/` | Claude Code skill for installing, repairing, and upgrading this toolchain. |

## Quick start

```powershell
# Audit first — read-only, names the exact broken component.
.\Install-Toolchain.ps1 -CheckOnly

# Install or repair.
.\Install-Toolchain.ps1
```

Then build the plugin binary, enable the plugin in your `.uproject`, and restart
the Editor with `-trace=default,cpu -tracehost=localhost`. Those steps are
project-specific and are documented in `Skills/ue-trace-install/SKILL.md`.

Prerequisites the installer checks but does not install: Node.js 18+, npm, git,
[uv](https://docs.astral.sh/uv/), and the `claude` CLI.

## What lands where

```
%USERPROFILE%\.local\bin\ue-insight-mcp.exe                      live Editor bridge
%USERPROFILE%\.local\share\ue-trace-mcp\<version>\
    app\node_modules\@mtuska\ue-trace-mcp\dist\index.js          offline analyzer entry
    native\windows-x64\TraceDigest.exe                           native analysis binary
    native\windows-x64\tbbmalloc.dll                             required side-by-side
    native\windows-x64\WinPixEventRuntime.dll                    required side-by-side
%USERPROFILE%\.claude\skills\ue-trace\                           analysis skill
%USERPROFILE%\.claude\skills\ue-trace-install\                   install skill
```

Both MCP servers are registered at user scope via `claude mcp add`. The
`ue-trace` entry sets `TRACE_DIGEST_BIN` to the staged executable, which is what
keeps the analyzer off the npx cache.

## Why the analyzer is patched

`@mtuska/ue-trace-mcp` 0.5.2 ships a working Linux path and a Windows path with
four rough edges. `Patches/ue-trace-mcp-0.5.2-windows.patch` addresses each:

1. **`dist/index.js`, `dist/server.js` — daemon is POSIX-only.** The persistent
   daemon uses a Unix socket. On win32 it either fails to start or leaves a
   stray process. The patch forces one-shot mode on Windows and leaves Linux
   untouched.

2. **`dist/digest.js` — one-shot output was read from stdout.** `TraceDigest.exe`
   interleaves UE log lines with its JSON, so parsing stdout is unreliable and a
   long log can trip the stdout cap and kill an otherwise healthy run. The patch
   routes each invocation through a unique `-out=<json>` file, reads the JSON
   from there, keeps only the tail of stdout for diagnostics, and cleans up the
   temp file on every exit path — success, failure, timeout, and spawn error.

3. **`dist/digest.js` — 60s timeout is too short.** A multi-hundred-megabyte
   `memalloc` trace routinely needs longer. The patch raises the win32 default to
   300s and honors a `TRACE_ONESHOT_TIMEOUT_MS` override.

4. **`dist/binary.js` — silent failure on an incomplete runtime.** `TraceDigest.exe`
   will not start without `tbbmalloc.dll` and `WinPixEventRuntime.dll` beside it,
   and the resulting error is opaque. The patch asserts all three are present and
   reports which one is missing.

Upstream may adopt any of these. Each is independently droppable — see the
upgrade procedure in `Skills/ue-trace-install/SKILL.md`.

## Integrity

The native runtime is downloaded from the upstream GitHub release and verified
against the SHA-256 in the npm package's own `binaries.json`, so the integrity
anchor stays inside the package boundary. A mismatch aborts the install before
extraction; it is a real problem, not something to work around.
