"""
UE Insight MCP - Unreal Insights Performance Analysis via Model Context Protocol

A MCP server for controlling UE Trace and analyzing performance data with:
- Trace start/stop/channel control
- .utrace file session management
- Performance data querying (frame times, CPU, GPU, memory, load times)
- Smart bottleneck analysis and regression detection

Port: 55559 (UEEditorMCP uses 55558)
"""

__version__ = "0.1.0"
__author__ = "yangskin"

from .connection import PersistentInsightConnection

__all__ = ["PersistentInsightConnection", "__version__"]
