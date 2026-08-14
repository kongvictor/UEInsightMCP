# UE Insight MCP — Setup Script
# Creates vendor directory with required Python wheels

$ErrorActionPreference = "Stop"

$PluginDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PythonDir = Join-Path $PluginDir "Python"
$VendorDir = Join-Path $PythonDir "vendor"

# Check if UEEditorMCP vendor exists (shared wheels)
$EditorVendorDir = Join-Path (Split-Path -Parent $PluginDir) "UEEditorMCP\Python\vendor"

if (Test-Path $EditorVendorDir) {
    Write-Host "Found UEEditorMCP vendor at: $EditorVendorDir"
    Write-Host "UEInsightMCP will share vendor wheels from UEEditorMCP."
    Write-Host ""
    Write-Host "To configure the MCP server, add to your MCP config:"
    Write-Host '  "ue-insight-mcp": {'
    Write-Host '    "command": "python",'
    Write-Host "    `"args`": [`"-m`", `"ue_insight_mcp.server_unified`"],"
    Write-Host "    `"cwd`": `"$PythonDir`""
    Write-Host '  }'
    exit 0
}

# Create vendor directory and download wheels
Write-Host "Creating vendor directory..."
New-Item -ItemType Directory -Force -Path $VendorDir | Out-Null

Write-Host "Downloading MCP wheels..."
python -m pip download mcp==1.26.0 --dest $VendorDir --only-binary=:all:

Write-Host ""
Write-Host "Setup complete!"
Write-Host ""
Write-Host "To configure the MCP server, add to your MCP config:"
Write-Host '  "ue-insight-mcp": {'
Write-Host '    "command": "python",'
Write-Host "    `"args`": [`"-m`", `"ue_insight_mcp.server_unified`"],"
Write-Host "    `"cwd`": `"$PythonDir`""
Write-Host '  }'
