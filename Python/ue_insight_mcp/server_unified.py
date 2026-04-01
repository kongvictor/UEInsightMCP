"""
UE Insight MCP Server — Unified 7-tool MCP interface for Trace/Insights.

Tools:
    insight_ping          — Test connection to UE Insight plugin
    insight_actions_search — Search available actions by keyword/tag
    insight_actions_schema — Get full schema + examples for an action
    insight_actions_run    — Execute a single action in UE
    insight_batch          — Execute multiple actions in one round-trip
    insight_resources_read — Read embedded documentation
    insight_logs_tail      — Tail recent UE editor logs

Port: 55559 (separate from UEEditorMCP's 55558)
"""

import sys
import os
import json
import logging
import time
from pathlib import Path
from typing import Any

# ── Vendor path setup ──────────────────────────────────────────────
# Only fall back to vendor .whl files if mcp is not already importable
# (uv/venv installs should take priority over zip-imported wheels)
_this_dir = Path(__file__).resolve().parent

try:
    import mcp  # noqa: F401 — test if already installed
except ImportError:
    _vendor_dir = _this_dir.parent / "vendor"
    _editor_vendor_dir = _this_dir.parent.parent.parent / "UEEditorMCP" / "Python" / "vendor"

    for vdir in [_vendor_dir, _editor_vendor_dir]:
        if vdir.is_dir():
            for whl in sorted(vdir.glob("*.whl")):
                whl_str = str(whl)
                if whl_str not in sys.path:
                    sys.path.insert(0, whl_str)
            break

from mcp.server.fastmcp import FastMCP

from .connection import PersistentInsightConnection, ConnectionConfig, get_connection
from .registry import get_registry

logging.basicConfig(level=logging.WARNING)
logger = logging.getLogger("ue_insight_mcp")

# ── MCP Server Instance ───────────────────────────────────────────
mcp = FastMCP(
    "ue-insight-mcp",
    instructions="Unreal Insights MCP v0.1.0 — AI-driven Trace & performance analysis",
)


# ── Tool Implementations ──────────────────────────────────────────

@mcp.tool(
    name="insight_ping",
    description="Test connection to Unreal Insight plugin. Returns pong if alive.",
)
def insight_ping() -> str:
    conn = get_connection()
    if not conn.is_connected:
        conn.connect()
    if conn.ping():
        return json.dumps({"status": "ok", "pong": True, "port": conn.config.port})
    return json.dumps({"status": "error", "error": "UE Insight not responding on port 55559"})


@mcp.tool(
    name="insight_actions_search",
    description=(
        "Search for available Insight actions by keyword or tag. "
        "Returns a ranked list of matching action IDs with descriptions. "
        "Use this FIRST to discover what actions exist before calling insight_actions_schema."
    ),
)
def insight_actions_search(
    query: str = "",
    tags: str = "",
    top_k: int = 10,
) -> str:
    """
    Args:
        query: Keyword search (e.g. 'trace start', 'channels', 'frame times')
        tags: Comma-separated tag filter (e.g. 'trace,control')
        top_k: Max results to return (default 10)
    """
    registry = get_registry()
    tag_list = [t.strip() for t in tags.split(",") if t.strip()] if tags else None
    results = registry.search(query, tags=tag_list, top_k=top_k)

    return json.dumps({
        "total_actions": registry.count,
        "query": query,
        "results": results,
        "count": len(results),
        "hint": "Use insight_actions_schema with an action id to get full parameters.",
    }, indent=2)


@mcp.tool(
    name="insight_actions_schema",
    description=(
        "Get the full input schema, examples, and metadata for a specific action. "
        "Call this AFTER insight_actions_search to learn the exact parameters "
        "before calling insight_actions_run."
    ),
)
def insight_actions_schema(action_id: str) -> str:
    """
    Args:
        action_id: The action id (e.g. 'trace.start', 'trace.channels.list')
    """
    registry = get_registry()
    schema = registry.schema(action_id)

    if schema is None:
        all_ids = registry.all_ids
        return json.dumps({
            "error": f"Action '{action_id}' not found",
            "available_actions": all_ids,
            "hint": "Use insight_actions_search to find the correct action id.",
        }, indent=2)

    return json.dumps(schema, indent=2)


@mcp.tool(
    name="insight_actions_run",
    description=(
        "Execute a single action in Unreal Engine. "
        "Use insight_actions_search + insight_actions_schema first to discover "
        "the required parameters. "
        "You may pass action parameters nested under 'params' key or as flat top-level keys."
    ),
)
def insight_actions_run(
    action_id: str,
    params: dict | None = None,
) -> str:
    """
    Args:
        action_id: The action to execute (e.g. 'trace.start')
        params: Action parameters as a dictionary
    """
    registry = get_registry()
    action_def = registry.get(action_id)

    if action_def is None:
        return json.dumps({
            "error": f"Unknown action: {action_id}",
            "hint": "Use insight_actions_search to find valid action ids.",
        })

    # Execute via TCP connection
    conn = get_connection()
    if not conn.is_connected:
        conn.connect()

    t0 = time.time()
    result = conn.send_command(action_def.command, params if params else None)
    elapsed = time.time() - t0

    response = result.to_dict()
    response["_action_id"] = action_id
    response["_elapsed_ms"] = round(elapsed * 1000, 1)

    return json.dumps(response, indent=2, default=str)


@mcp.tool(
    name="insight_batch",
    description=(
        "Execute multiple actions in a SINGLE TCP round-trip. "
        "Stops at first failure unless continue_on_error is true. Max 50 actions."
    ),
)
def insight_batch(
    actions: list[dict],
    continue_on_error: bool = False,
) -> str:
    """
    Args:
        actions: List of {action_id, params} dicts
        continue_on_error: If true, continue executing after a failure
    """
    if not actions or len(actions) > 50:
        return json.dumps({"error": "Batch must contain 1-50 actions"})

    registry = get_registry()
    conn = get_connection()
    if not conn.is_connected:
        conn.connect()

    # Build batch command list
    commands = []
    for item in actions:
        aid = item.get("action_id", "")
        action_def = registry.get(aid)
        if action_def is None:
            return json.dumps({"error": f"Unknown action in batch: {aid}"})
        commands.append({
            "type": action_def.command,
            "params": item.get("params", {}),
        })

    # Send as batch_execute
    batch_params = {
        "commands": commands,
        "continue_on_error": continue_on_error,
    }

    t0 = time.time()
    result = conn.send_command("batch_execute", batch_params)
    elapsed = time.time() - t0

    response = result.to_dict()
    response["_elapsed_ms"] = round(elapsed * 1000, 1)
    return json.dumps(response, indent=2, default=str)


@mcp.tool(
    name="insight_resources_read",
    description=(
        "Read embedded documentation and reference material. "
        "Available resources: conventions.md, error_codes.md, trace_channels.md"
    ),
)
def insight_resources_read(resource: str = "conventions.md") -> str:
    """
    Args:
        resource: Resource filename (conventions.md, error_codes.md, trace_channels.md)
    """
    resources_dir = _this_dir / "resources"
    resource_path = resources_dir / resource

    if not resource_path.exists():
        available = [f.name for f in resources_dir.glob("*.md")] if resources_dir.exists() else []
        return json.dumps({
            "error": f"Resource '{resource}' not found",
            "available": available,
        })

    content = resource_path.read_text(encoding="utf-8")
    return json.dumps({
        "resource": resource,
        "content": content,
        "length": len(content),
    })


@mcp.tool(
    name="insight_logs_tail",
    description=(
        "Tail recent UE editor logs. "
        "Source: 'python' (MCP command log), 'editor' (UE editor log), or 'both'."
    ),
)
def insight_logs_tail(
    source: str = "editor",
    lines: int = 50,
    category: str = "",
    severity: str = "all",
    after_seq: int = 0,
) -> str:
    """
    Args:
        source: 'python', 'editor', or 'both'
        lines: Max lines to return (default 50)
        category: Filter by log category (e.g. 'LogInsightMCP', 'LogTrace')
        severity: Minimum severity: 'all', 'log', 'warning', 'error'
        after_seq: Only return entries after this sequence id (for polling)
    """
    result_parts = []

    if source in ("editor", "both"):
        conn = get_connection()
        if conn.is_connected:
            params = {
                "max_lines": lines,
                "after_seq": after_seq,
            }
            if category:
                params["categories"] = category
            if severity != "all":
                params["min_severity"] = severity

            cmd_result = conn.send_command("get_editor_logs", params)
            if cmd_result.success:
                result_parts.append({
                    "source": "editor",
                    "entries": cmd_result.data.get("entries", []),
                    "last_seq": cmd_result.data.get("last_seq", 0),
                    "truncated": cmd_result.data.get("truncated", False),
                })
            else:
                result_parts.append({
                    "source": "editor",
                    "error": cmd_result.error,
                })
        else:
            result_parts.append({
                "source": "editor",
                "error": "Not connected to UE Insight",
            })

    if source in ("python", "both"):
        # Python-side command log (simplified)
        result_parts.append({
            "source": "python",
            "entries": [],
            "note": "Python command logging not yet implemented",
        })

    return json.dumps({
        "results": result_parts,
        "timestamp": time.time(),
    }, indent=2, default=str)


# ── Entry Point ───────────────────────────────────────────────────

def main():
    """Run the Insight MCP server via stdio."""
    logger.info("Starting UE Insight MCP Server v0.1.0 on stdio...")
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
