# UEInsightMCP — 安装指南

UEInsightMCP 的完整安装步骤。

---

## 前置条件

- **Unreal Engine**：UE5.4+（完整功能）或 UE4.26 RDCSP（仅 Trace 控制）
- **Python**：3.10+（用于 MCP 桥接层）
- **MCP 客户端**：WorkBuddy、Claude Desktop、Cursor 或任何兼容 MCP 的 AI 助手

---

## 第一步：安装 C++ 插件

将整个 `UEInsightMCP/` 文件夹复制到项目的 `Plugins/` 目录：

```
你的项目/
└── Plugins/
    ├── UEEditorMCP/        ← 可选，用于编辑器自动化
    └── UEInsightMCP/       ← 本插件
```

然后重新生成项目文件：

```bash
# Windows (Visual Studio)
# 右键点击 .uproject → "Generate Visual Studio project files"

# 或通过命令行
"C:\Program Files\Epic Games\UE_5.4\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" \
  -projectfiles -project="你的项目.uproject" -game -rocket -progress
```

编译项目（Development Editor 配置）。

### 验证插件已加载

启动编辑器后，在 Output Log 中检查以下日志：

```
LogInsightMCP: UEInsightMCP: Module starting up
LogInsightMCP: UEInsightMCP: Bridge initializing
LogInsightMCP: UEInsightMCP: Registered 20 action handlers
LogInsightMCP: UEInsightMCP: Server started on port 55559 (max 8 clients)
```

四行日志都应出现。如未出现，参见 [问题排查](#问题排查)。

也可以在 **Edit → Plugins** 中搜索 "UEInsightMCP" 确认已启用。

---

## 第二步：安装 Python 依赖

Python MCP 桥接层需要 `mcp` 包。选择以下任一方式：

### 方式 A：pip 安装（推荐）

```bash
cd <插件路径>/Python
pip install -e .
```

### 方式 B：安装脚本

```powershell
cd <插件路径>
.\setup_mcp.ps1
```

### 方式 C：使用 vendor wheels（零安装）

如果同一 `Plugins/` 目录下已安装 UEEditorMCP，Python 桥接层会在启动时自动发现其 `vendor/` wheels。无需额外安装 — 自动回退到打包的 `.whl` 文件。

**依赖查找顺序：**
1. 系统已安装的 `mcp` 包（pip/uv）
2. `UEInsightMCP/Python/vendor/*.whl`
3. `UEEditorMCP/Python/vendor/*.whl`（兄弟插件）

---

## 第三步：配置 MCP 客户端

将 UEInsightMCP 添加到你的 AI 助手的 MCP 配置中。

### WorkBuddy

编辑 `~/.workbuddy/mcp.json`：

```json
{
  "mcpServers": {
    "ue-insight-mcp": {
      "command": "python",
      "args": ["-m", "ue_insight_mcp.server_unified"],
      "cwd": "C:/Users/<你的用户名>/Documents/Unreal Projects/<你的项目>/Plugins/UEInsightMCP/Python"
    }
  }
}
```

### Claude Desktop

编辑 `~/Library/Application Support/Claude/claude_desktop_config.json`（macOS）或 `%APPDATA%/Claude/claude_desktop_config.json`（Windows）：

```json
{
  "mcpServers": {
    "ue-insight-mcp": {
      "command": "python",
      "args": ["-m", "ue_insight_mcp.server_unified"],
      "cwd": "C:/Users/<你的用户名>/Documents/Unreal Projects/<你的项目>/Plugins/UEInsightMCP/Python"
    }
  }
}
```

### Cursor

编辑项目根目录下的 `.cursor/mcp.json`：

```json
{
  "mcpServers": {
    "ue-insight-mcp": {
      "command": "python",
      "args": ["-m", "ue_insight_mcp.server_unified"],
      "cwd": "./Plugins/UEInsightMCP/Python"
    }
  }
}
```

> **注意**：将 `<你的用户名>` 和 `<你的项目>` 替换为实际路径。`cwd` 必须指向插件内的 `Python/` 目录。

---

## 第四步：验证连接

### 通过 AI 助手快速测试

在 AI 助手中输入：

```
用 insight_ping 测试一下连接
```

预期响应：
```json
{"status": "ok", "pong": true, "port": 55559}
```

### 手动 TCP 测试

也可以不通过 MCP 桥接层，直接测试原始 TCP 连接：

```bash
cd <插件路径>
python test_tcp.py
```

这会遍历所有 Phase 1 Action（ping → trace.status → channels.list → start → bookmark → stop → close）。

### 完整端到端测试

```bash
cd <插件路径>
python test_all_queries.py
```

这会测试全部 20 个 Action：session.list → session.open → 所有 query.* → session.close。

---

## 双插件配置（推荐）

要获得完整的 AI 辅助开发闭环，建议同时安装两个 MCP 插件：

```
Plugins/
├── UEEditorMCP/        ← 端口 55558：编辑器自动化（蓝图、资产、代码）
└── UEInsightMCP/       ← 端口 55559：Trace 和性能分析
```

两个插件的 MCP 配置：

```json
{
  "mcpServers": {
    "ue-editor-mcp": {
      "command": "python",
      "args": ["-m", "ue_editor_mcp.server"],
      "cwd": "./Plugins/UEEditorMCP/Python"
    },
    "ue-insight-mcp": {
      "command": "python",
      "args": ["-m", "ue_insight_mcp.server_unified"],
      "cwd": "./Plugins/UEInsightMCP/Python"
    }
  }
}
```

两个插件共享同一套 `vendor/` wheels，只需一份依赖副本。

---

## UE4 RDCSP 安装说明

UE4.26 RDCSP 构建使用同一份插件源码，有两个自动差异：

1. **Build.cs**：`EditorScriptingUtilities` 被排除（UE4 中不可用）
2. **.uplugin**：使用 `WhitelistPlatforms` 替代 `PlatformAllowList`
3. **Phase 2 Action**：编译为空操作（返回 "需要 UE5" 错误）

无需手动修改 — `#if ENGINE_MAJOR_VERSION >= 5` 宏守卫自动处理一切。

---

## 问题排查

### "Server started" 日志没有出现

1. 检查 **Edit → Plugins** — UEInsightMCP 是否已启用？
2. 在 Output Log 中查找编译错误（`LogInsightMCP` 分类）
3. 确保 `UEInsightMCP.uplugin` 中 `"Type": "Editor"` 和 `"LoadingPhase": "Default"`

### "Port 55559 is already in use"

另一个带有 UEInsightMCP 的 UE 编辑器实例正在运行，或其他进程占用了端口。

```powershell
# 查找占用端口的进程
netstat -ano | findstr 55559

# 如需终止
taskkill /PID <pid> /F
```

### Python 桥接层无法连接

1. 确认编辑器正在运行且显示了 "Server started" 日志
2. 手动测试：`python test_tcp.py`
3. 检查防火墙是否阻止了 `127.0.0.1:55559`
4. 确保 MCP 配置中的 `cwd` 指向正确的 `Python/` 目录

### "Module 'mcp' not found"

`mcp` Python 包未安装且未找到 vendor wheels。

```bash
pip install mcp
# 或：pip install -e <插件路径>/Python
```

### 插件编译通过但没有注册 Action

检查 `TraceSessionActions.h/cpp` 和 `TraceQueryActions.h/cpp` 是否包含在构建中。`Build.cs` 会自动发现 `Private/Actions/` 下的所有 `.cpp` 文件。

---

## 连接参数参考

### Python → UE（TCP）

| 参数 | 值 |
|------|---|
| 主机 | `127.0.0.1` |
| 端口 | `55559` |
| 协议 | 4 字节大端长度头 + UTF-8 JSON |
| 超时 | 120 秒 |
| 心跳 | 5 秒间隔 |
| 重连 | 指数退避，最多 5 次 |
| 最大重连延迟 | 30 秒 |

### UE TCP 服务器

| 参数 | 值 |
|------|---|
| 监听地址 | `127.0.0.1:55559` |
| 最大客户端数 | 8 |
| 连接超时 | 300 秒 |
| 接收缓冲区 | 1 MB |
| 游戏线程超时 | 240 秒 |

---

*安装完成后，参见 [USAGE_zh.md](USAGE_zh.md) 了解使用方法。*
