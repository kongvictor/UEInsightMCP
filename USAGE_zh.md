# UEInsightMCP — 使用指南

如何在 Unreal 编辑器中使用 UEInsightMCP 进行性能分析。

> **前提**：插件已安装、MCP 已配置。如未完成，请先参阅 [INSTALL_zh.md](INSTALL_zh.md)。

---

## 目录

- [快速开始](#快速开始)
- [编辑器内的工作方式](#编辑器内的工作方式)
- [工作流 1：实时性能分析](#工作流-1实时性能分析)
- [工作流 2：离线 Trace 分析](#工作流-2离线-trace-分析)
- [工作流 3：资产加载分析](#工作流-3资产加载分析)
- [工作流 4：快速健康检查](#工作流-4快速健康检查)
- [Action 参数详解](#action-参数详解)
- [最佳实践](#最佳实践)
- [输出格式解读](#输出格式解读)
- [问题排查](#问题排查)

---

## 快速开始

安装完成后，插件**全自动运行** — 不需要点按钮，不需要打开窗口。直接对 AI 助手说：

```
"开始录 Trace，让我玩 30 秒，然后分析性能"
```

AI 通过 MCP 工具在后台处理一切。

---

## 编辑器内的工作方式

### 打开编辑器时发生了什么

1. UE 加载 `UEInsightMCP` 插件模块
2. `UInsightBridge`（EditorSubsystem）自动初始化
3. TCP 服务器在**端口 55559** 上开始监听（仅 localhost）
4. 20 个 Action 处理器注册完毕，准备接收命令
5. Output Log 中出现：

```
LogInsightMCP: UEInsightMCP: Server started on port 55559 (max 8 clients)
LogInsightMCP: UEInsightMCP: Registered 20 action handlers
```

### 你不需要做的事

- ❌ 不需要打开任何特殊窗口或面板
- ❌ 不需要在 UI 中手动启用插件（自动启用）
- ❌ 不需要手动启动任何服务
- ❌ 不需要自己运行 Python 桥接层（MCP 客户端会自动启动）

### 什么跑在哪个线程

| Action 类型 | 线程 | 原因 |
|-------------|------|------|
| `trace.*`（start/stop/channels/bookmark） | **游戏线程** | 必须与引擎 Trace API 交互 |
| `session.*`（list/open/info/close） | **TCP 工作线程** | 读取 .utrace 文件，不需要引擎状态 |
| `query.*`（所有 9 个查询） | **TCP 工作线程** | 纯数据分析，不接触编辑器 |

**这意味着**：运行大量查询（如解析 500MB Trace）**不会冻结编辑器**。分析运行时你可以继续工作。

---

## 工作流 1：实时性能分析

**场景**：你想对某个特定的游戏时刻进行性能分析。

### 分步操作

```
步骤 1：启用通道并开始录制
─────────────────────────────────────────────
AI → trace.channels.toggle(channels: "cpu,gpu,frame,loadtime,bookmark", enable: true)
AI → trace.start(channels: "cpu,gpu,frame,loadtime,bookmark", output: "file")

步骤 2：执行你要分析的操作
─────────────────────────────────────────────
你 → 在编辑器中 Play (PIE)，加载关卡，生成 Actor 等
AI → trace.bookmark.add(label: "entered_combat")        ← 可选：标记关键时刻
     ... 继续操作 ...
AI → trace.bookmark.add(label: "boss_defeated")

步骤 3：停止录制
─────────────────────────────────────────────
AI → trace.stop
     返回：{ file: "C:/.../Saved/TraceSessions/Trace_20260330_112000.utrace", size_mb: 45.2 }

步骤 4：打开进行分析
─────────────────────────────────────────────
AI → session.open(file: "<步骤 3 的文件路径>")
AI → session.info                             ← 检查可用数据

步骤 5：查询你需要的数据
─────────────────────────────────────────────
AI → query.frame_times                        ← 整体 FPS 统计
AI → query.hitches(threshold_ms: 33.33)       ← 找卡顿
AI → query.cpu_threads(top_n: 10)             ← 热点线程和函数
AI → query.bookmarks                          ← 验证你的标记是否记录

步骤 6：清理
─────────────────────────────────────────────
AI → session.close
```

### 对话示例

```
你:   "帮我分析一下 PIE 启动性能"
AI:   我来设置通道和开始录制。请按 Play。

      [trace.channels.toggle → trace.start]

你:   "好了，已经加载完了，我在主菜单了"
AI:   [trace.bookmark.add("main_menu_reached") → trace.stop]
      录制完成（38.7 MB）。我来分析...

      [session.open → query.frame_times → query.loadtime → query.hitches]

      结果：
      - 22.3 秒内共 1,247 帧（平均 55.9 FPS）
      - 47 次卡顿 > 33ms（主要在初始资产加载阶段）
      - 最大瓶颈：/Game/Maps/MainMenu 加载了 89 个包，耗时 12.1 秒
      - 最重资产：ControlRig_Mannequin（153ms，4,283 个 exports）
```

---

## 工作流 2：离线 Trace 分析

**场景**：你已有 `.utrace` 文件（来自 CI、其他机器或之前的会话）。

```
步骤 1：列出可用的 Trace 文件
─────────────────────────────────────────────
AI → session.list(limit: 10)
     返回 {项目}/Saved/TraceSessions/ 下最新的 .utrace 文件

步骤 2：打开指定的 Trace
─────────────────────────────────────────────
AI → session.open(file: "C:/Traces/build_1234.utrace")
AI → session.info
     显示：15,000 帧，24 线程，timing + loadtime + memory provider 可用

步骤 3：运行查询
─────────────────────────────────────────────
AI → query.frame_times(frame_type: "game")
AI → query.cpu_threads(filter_name: "GameThread", top_n: 20)
AI → query.loadtime(sort_by: "duration", top_n: 50)
AI → query.loadtime_deps(package: "/Game/Maps/")
AI → query.memory(tag: "Texture")
AI → query.counters(filter: "DrawCalls", include_values: true)

步骤 4：关闭
─────────────────────────────────────────────
AI → session.close
```

### 使用自定义目录

默认情况下，`session.list` 搜索 `{项目}/Saved/TraceSessions/`。要搜索其他位置：

```
AI → session.list(directory: "D:/CI/TraceOutputs/", limit: 20)
```

---

## 工作流 3：资产加载分析

**场景**：启动很慢，你需要了解加载时间线。

> **关键**：要获取完整的加载时间数据，`loadtime` 通道必须在**录制开始前**启用。如果要分析引擎启动阶段，请使用命令行参数（见 [最佳实践](#最佳实践)）。

```
AI → session.open(file: "<你的 Trace 文件>")

# 时间线视图：按时间顺序排列的加载请求
AI → query.loadtime(sort_by: "start_time", top_n: 100)

# 最慢的是什么
AI → query.loadtime(sort_by: "duration", top_n: 20)

# 依赖图：为什么加载了这个包？
AI → query.loadtime_deps(package: "/Game/Characters/Hero")

# 内存影响
AI → query.memory(tag: "Texture")

AI → session.close
```

### AI 能告诉你什么

基于 `query.loadtime` + `query.loadtime_deps` 的数据，AI 可以：
- 识别**加载阶段**（引擎初始化 → 配置 → 地图 → 游戏资产）
- 找到**串行瓶颈**（阻塞关键路径的单个包）
- 检测**意外依赖**（为什么菜单加载时在加载 Audio？）
- 计算**每阶段耗时**和并行效率
- 建议**优化策略**（异步加载、预加载、裁剪依赖）

---

## 工作流 4：快速健康检查

**场景**：一切正常吗？快速确认一下。

```
# 单命令检查
AI → insight_batch([
  {action_id: "trace.status"},
  {action_id: "trace.channels.list"}
])

# 返回当前 Trace 状态 + 所有可用通道
```

或者直接说：
```
你:   "检查一下 Insight MCP 是否正常"
AI:   [insight_ping → trace.status → trace.channels.list]
      ✅ 已连接。当前未录制。47 个通道可用。
```

---

## Action 参数详解

### trace.start

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `channels` | string | `cpu,gpu,frame,memory,loadtime` | 逗号分隔的通道名 |
| `output` | string | `file` | `file`（本地 .utrace）或 `server`（流式发送到 Trace Server） |
| `host` | string | `127.0.0.1` | Trace Server 地址（仅 server 模式） |

### trace.channels.toggle

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `channels` | string | *（必填）* | 逗号分隔的通道名 |
| `enable` | boolean | `true` | `true` 启用，`false` 禁用 |

### trace.bookmark.add

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `label` | string | *（必填）* | 书签文本（出现在 `query.bookmarks` 中） |

### session.list

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `directory` | string | `{项目}/Saved/TraceSessions/` | 搜索目录 |
| `limit` | integer | `20` | 最大返回文件数（最新优先） |

### session.open

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `file` | string | *（必填）* | `.utrace` 文件完整路径 |

### query.frame_times

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `frame_type` | string | `game` | `game` 或 `render` |
| `limit` | integer | `10` | 返回最差帧数量 |

### query.cpu_threads

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `top_n` | integer | `20` | 每线程 Top 计时器数 |
| `filter_name` | string | — | 按名称过滤线程（部分匹配） |

### query.gpu_timing

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `top_n` | integer | `20` | 返回 Top GPU 计时器数 |

### query.loadtime

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `top_n` | integer | `30` | 最大返回请求数 |
| `sort_by` | string | `duration` | `duration`（最慢优先）或 `start_time`（按时间顺序） |

### query.loadtime_deps

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `package` | string | — | 按名称过滤包（部分匹配） |
| `top_n` | integer | `20` | 最大返回包数 |

### query.memory

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `tag` | string | — | 按名称过滤内存标签（部分匹配） |
| `tracker_id` | integer | `0` | Tracker ID（0 = 默认 tracker） |

### query.counters

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `filter` | string | — | 按名称过滤计数器（部分匹配） |
| `group` | string | — | 按组过滤（部分匹配） |
| `include_values` | boolean | `false` | 是否包含采样值 |
| `max_samples` | integer | `100` | 每个计数器最大采样数 |

### query.bookmarks

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `filter` | string | — | 按文本过滤书签（部分匹配） |

### query.hitches

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `threshold_ms` | number | `33.33` | 帧时间阈值（毫秒） |
| `limit` | integer | `50` | 最大返回卡顿数 |
| `frame_type` | string | `game` | `game` 或 `render` |

---

## 最佳实践

### 1. 录制前先启用通道

通道必须在 `trace.start` 之前启用才能完整采集数据。推荐顺序：

```
trace.channels.toggle(channels: "cpu,gpu,frame,loadtime,bookmark,memory", enable: true)
trace.start(channels: "cpu,gpu,frame,loadtime,bookmark,memory")
```

### 2. 启动阶段分析请用命令行参数

如果需要采集引擎启动阶段的数据（在插件初始化之前），在引擎启动参数中添加：

```
-trace=cpu,frame,log,bookmark,loadtime,assetloadtime,file
```

这会在引擎启动时就开始录制，早于任何插件或游戏代码运行。

### 3. 用书签标记阶段

在关键时刻插入书签，方便后续分析：

```
trace.bookmark.add(label: "level_streaming_start")
trace.bookmark.add(label: "all_players_spawned")
trace.bookmark.add(label: "round_1_begin")
```

AI 可以利用这些书签来分割时间线，计算每阶段的指标。

### 4. 按 start_time 排序查看加载时间线

```
query.loadtime(sort_by: "start_time", top_n: 100)
```

这提供时间顺序视图 — AI 可以重建加载顺序，识别串行瓶颈。

### 5. 组合查询进行交叉分析

真正的威力在于在一次对话中组合多个查询：

```
query.frame_times      → "平均 FPS 45，但有 23 次卡顿"
query.hitches          → "卡顿集中在第 1200-1500 帧"
query.cpu_threads      → "这些帧的 GameThread 飙升：GC + 流式加载"
query.loadtime         → "第 1200 帧有 300MB 纹理流式请求"
```

AI 会自动将这些线索串联起来。

### 6. 大 Trace 文件：session.open 需要耐心

打开 500MB+ 的 Trace 文件时，`session.open` 可能需要 10-30 秒来解析所有数据。进度会记录在 UE 的 Output Log 中。后续查询会很快（读取已在内存中的数据）。

### 7. 务必关闭会话

分析完成后，调用 `session.close` 释放内存。一个打开的会话会将所有解析后的 Trace 数据保存在 RAM 中。

---

## 输出格式解读

### query.frame_times 输出

```json
{
  "summary": {
    "total_frames": 1247,
    "avg_ms": 17.89,      // 平均帧时间
    "avg_fps": 55.9,
    "min_ms": 8.2,
    "max_ms": 312.5,
    "p95_ms": 28.4,       // 第 95 百分位
    "p99_ms": 67.1        // 第 99 百分位 — 如果远大于 p95，说明有离散的严重卡顿
  },
  "distribution": [        // FPS 分桶
    {"label": "120+ fps", "count": 0, "percentage": 0.0},
    {"label": "90-120",   "count": 45, "percentage": 3.6},
    {"label": "60-90",    "count": 812, "percentage": 65.1},
    {"label": "30-60",    "count": 343, "percentage": 27.5},
    {"label": "<30 fps",  "count": 47, "percentage": 3.8}
  ],
  "worst_frames": [        // 最差的单帧
    {"frame_index": 1234, "duration_ms": 312.5, "fps": 3.2},
    ...
  ]
}
```

**关键洞察**：如果 `p99` 远大于 `p95`，说明你偶尔有严重卡顿，但整体性能稳定。

### query.hitches 输出

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

**严重度等级**（约定）：
- `> 100ms` — 严重（肉眼可见冻结）
- `> 50ms` — 较重（明显卡顿）
- `> 33.33ms` — 轻微（帧率低于 30fps）

### query.loadtime 输出

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

## 问题排查

### "Trace is already running"

先调用 `trace.stop`，再调用 `trace.start`。用 `trace.status` 检查当前状态。

### "No session open"

运行任何 `query.*` Action 前必须先调用 `session.open` 打开一个 `.utrace` 文件。用 `session.list` 查找可用的 Trace 文件。

### "Not connected to UE Insight"

1. 确认 UEInsightMCP 插件在 UE 编辑器中已启用
2. 确认端口 55559 未被占用（`netstat -ano | findstr 55559`）
3. 用 `insight_ping` 测试

### LoadTimeProfiler 没有数据

- 确保 `loadtime` 通道在 **Trace 录制开始前**就已启用
- 启动阶段分析请用引擎命令行：`-trace=cpu,frame,log,bookmark,loadtime,assetloadtime,file`
- 或在录制前动态切换通道：
  ```
  trace.channels.toggle(channels: "loadtime,assetloadtime,file", enable: true)
  ```
- 插件会在创建分析会话时自动激活 `LoadTimeProfilerModule`（修复 UE5 `WITH_EDITOR` 默认禁用的问题）

### 查询 Action 超时

- 大 Trace 文件（>100MB）在 `session.open` 时解析可能耗时较长
- 打开后查询应该很快（内存中的数据）
- 查询 Action 在 TCP 工作线程运行，不会阻塞编辑器

### 会话打开了但 Provider 缺失

调用 `session.open` 后，运行 `session.info` 查看可用的 Provider：

```json
{
  "has_timing_profiler": true,
  "has_loadtime_profiler": true,   // false = 录制时 loadtime 通道未启用
  "has_counter_provider": true,
  "has_bookmark_provider": true
}
```

如果某个 Provider 为 `false`，说明录制时对应通道未启用。需要重新录制并启用正确的通道。

### 内存占用过高

每个打开的分析会话会将所有解析后的 Trace 数据保存在 RAM 中。100MB 的 `.utrace` 文件在内存中可能展开到 500MB+。分析完毕后务必调用 `session.close`。

---

## 内置 TCP 命令

以下是由 TCP 服务器直接处理的低级命令（非 Action 处理器）：

| 命令 | 说明 |
|------|------|
| `ping` | 心跳，返回 `{pong: true}` |
| `close` | 关闭 TCP 连接 |
| `get_context` | 获取当前 InsightContext 状态的 JSON |

### 日志捕获

插件通过环形缓冲区捕获 UE 编辑器日志：

| 参数 | 值 |
|------|---|
| 容量 | 10,000 条 / 5 MB |
| 最大消息长度 | 8,192 字符（超长截断） |
| 线程安全 | 是 |
| 过滤方式 | 分类、严重度、关键词 |

通过 `insight_logs_tail` MCP 工具访问：

```
insight_logs_tail(source: "editor", lines: 50, category: "LogInsightMCP", severity: "warning")
```

---

*架构概述和 Action 列表参见 [README_zh.md](README_zh.md)。*
*安装步骤参见 [INSTALL_zh.md](INSTALL_zh.md)。*
