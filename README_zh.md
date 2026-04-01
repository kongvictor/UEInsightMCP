# UEInsightMCP

**Unreal Insights MCP 插件** — 让 AI 助手通过 [MCP 协议](https://modelcontextprotocol.io/) 控制 UE Trace 录制和查询性能数据。

> 版本 0.2.0 · Beta · UE5.4+（完整功能）/ UE4.26 RDCSP（仅 Trace 控制）

---

## 为什么需要它

Unreal Insights 功能强大，但只有 GUI 界面 — 你得拖时间轴、点标签页、肉眼找异常。没有任何编程接口能查询 Trace 数据。

UEInsightMCP 解决了这个问题。它通过 MCP 暴露 **20 个 Action**，让 AI 助手可以：

1. **启停** Trace 录制，精确控制通道
2. **打开** 任意 `.utrace` 文件进行分析
3. **查询** 帧时间、CPU 线程、GPU 计时、资产加载、内存、计数器、书签和卡顿
4. **交叉分析** 多个数据源，在一次对话中完成
5. **生成** 结构化性能报告，自动定位瓶颈

原来需要 30–120 分钟的手动 Insights UI 操作，现在 **3–5 分钟** AI 辅助分析即可完成。

### 实际案例

```
你:   "录个 Trace，我跑一会儿 PIE，然后告诉我哪里慢"
AI:   trace.start → (你操作) → trace.stop → session.open → query.loadtime
      → query.cpu_threads → query.hitches
      → "TopDownArena Experience 加载了 126 个包，耗时 21.3s（占总时间 79.8%）。
         瓶颈：ControlRig CR_Mannequin_FootPlant（4283 exports，153ms）。"
```

---

## 架构

```
AI 助手（WorkBuddy / Claude / Cursor 等）
    │ MCP (stdio)
    ▼
Python MCP 桥接层  ─── 7 个 MCP 工具（search → schema → run）
    │ TCP (端口 55559)
    ▼
C++ UE 插件（UEditorSubsystem）
    ├── InsightServer  ── TCP 监听 + 每客户端独立线程
    ├── InsightBridge  ── 命令路由 + Action 分发
    ├── InsightContext  ── Trace 状态 + 分析会话管理
    └── Actions/
        ├── TraceControlActions  ── 6 个 Trace Action + batch_execute
        ├── TraceSessionActions  ── 4 个会话管理 Action
        └── TraceQueryActions   ── 9 个数据查询 Action
```

与 [UEEditorMCP](../UEEditorMCP/)（端口 55558）在同一个编辑器进程中并行运行。两者组成 **写代码 → 跑性能 → 定位问题 → 修复** 的 AI 辅助开发闭环。

### 启动时序

```
UE 编辑器启动
  │
  ├─ ① FUEInsightMCPModule::StartupModule()      // 模块加载，启动日志捕获
  │
  └─ ② UInsightBridge::Initialize()               // EditorSubsystem 自动创建
        ├─ RegisterActions()                       // 注册 20 个 Action 处理器
        └─ FInsightServer::Start(端口 55559)        // TCP 服务器开始监听
```

插件完全自动化 — 不需要任何手动初始化。打开编辑器项目后，TCP 服务器立即开始监听。

---

## MCP 工具

Python 桥接层暴露 **7 个 MCP 工具**，遵循渐进式发现模式：

| 工具 | 说明 |
|------|------|
| `insight_ping` | 测试与 UE Insight 插件的连接 |
| `insight_actions_search` | 按关键词/标签搜索可用 Action |
| `insight_actions_schema` | 获取 Action 的完整参数 Schema + 示例 |
| `insight_actions_run` | 执行单个 Action |
| `insight_batch` | 一次 TCP 往返执行多个 Action（最多 50 个） |
| `insight_resources_read` | 读取内置文档（conventions / error_codes / trace_channels） |
| `insight_logs_tail` | 查看 UE 编辑器日志 |

### 发现流程

```
1. insight_actions_search("loadtime")     → 找到 Action ID
2. insight_actions_schema("query.loadtime") → 了解参数
3. insight_actions_run("query.loadtime", {top_n: 50, sort_by: "start_time"}) → 获取数据
```

---

## 全部 20 个 Action

### Trace 控制（Phase 1）— 6 个

| Action | 说明 | 类型 |
|--------|------|------|
| `trace.start` | 开始录制（文件或服务器模式） | 写入 |
| `trace.stop` | 停止录制，返回文件信息 | 写入 |
| `trace.status` | 获取当前 Trace 状态 | 只读 |
| `trace.channels.list` | 列出所有注册通道及启用状态 | 只读 |
| `trace.channels.toggle` | 启用/禁用指定通道 | 写入 |
| `trace.bookmark.add` | 在当前时间点插入命名书签 | 写入 |

### 会话管理（Phase 2）— 4 个

| Action | 说明 | 类型 |
|--------|------|------|
| `session.list` | 列出 Trace 目录下的 `.utrace` 文件 | 只读 |
| `session.open` | 打开 `.utrace` 文件进行分析 | 只读 |
| `session.info` | 获取会话详情（帧数、线程、Provider） | 只读 |
| `session.close` | 关闭会话，释放资源 | 写入 |

### 数据查询（Phase 2）— 9 个

| Action | 说明 | 类型 |
|--------|------|------|
| `query.frame_times` | 帧时间统计：avg/p95/p99、FPS 分布、最差帧 | 只读 |
| `query.cpu_threads` | CPU 线程计时聚合，每线程 Top 计时器 | 只读 |
| `query.gpu_timing` | GPU 计时数据，按总时间排序的 Top GPU 计时器 | 只读 |
| `query.loadtime` | 资产加载时间，按耗时或开始时间排序 | 只读 |
| `query.loadtime_deps` | 资产依赖图，含每个包的计时 | 只读 |
| `query.memory` | LLM 内存标签：追踪器和最新值 | 只读 |
| `query.counters` | 命名计数器，可选值采样 | 只读 |
| `query.bookmarks` | 会话中所有 `TRACE_BOOKMARK` 条目 | 只读 |
| `query.hitches` | 卡顿检测：超阈值帧，按严重度排序 | 只读 |

### 工具类 — 1 个

| Action | 说明 |
|--------|------|
| `batch_execute` | 批量执行多个 Action |

---

## UE4 / UE5 兼容性

同一份代码通过 `#if ENGINE_MAJOR_VERSION >= 5` 同时支持两个引擎版本：

| 特性 | UE5 | UE4 RDCSP |
|------|-----|-----------|
| **Trace 控制**（Phase 1） | ✅ `FTraceAuxiliary` | ✅ `Trace::WriteTo/SendTo/Stop` |
| **会话 & 查询**（Phase 2） | ✅ 完整 `IAnalysisSession` API | ❌ 不可用 |
| 通道枚举 | `UE::Trace::EnumerateChannels` | 硬编码 20+ 已知通道 |
| 命名空间 | `UE::Trace::` | `Trace::` |
| EditorScriptingUtilities | ✅ 需要 | ❌ 条件排除 |
| RDCSP 扩展 | — | `GetConnectPort` / `GetMemoryUsed` |

---

## 技术细节

### TCP 协议

- **端口**：55559（UEEditorMCP 使用 55558）
- **消息格式**：4 字节大端长度头 + UTF-8 JSON 正文
- **接收缓冲区**：1 MB
- **最大客户端数**：8
- **连接超时**：300 秒

### 线程模型

Action 声明 `RequiresGameThread()`：
- **Trace 控制 Action** → 游戏线程（必须与 FTraceAuxiliary 交互）
- **会话 & 查询 Action** → TCP 工作线程（不阻塞游戏线程）

这保证了大量查询不会卡住编辑器。

### 崩溃保护

- **Windows**：SEH（`__try / __except`）捕获访问违例
- **所有平台**：C++ 异常捕获（`try / catch`）
- 崩溃返回 `crash_prevented` 错误，不会导致编辑器崩溃

### 分析模块自动激活

会话查询会自动通过 `IModuleService::SetModuleEnabled()` 启用 UE5 分析模块（TimingProfiler、LoadTimeProfiler、MemoryProfiler 等）。这修复了常见问题：当 `WITH_EDITOR` 为 true 时，`LoadTimeProfiler` 默认禁用导致返回 nullptr。

---

## 项目结构

```
UEInsightMCP/
├── UEInsightMCP.uplugin
├── README.md / README_zh.md        ← 项目概述
├── INSTALL.md / INSTALL_zh.md      ← 安装指南
├── USAGE.md / USAGE_zh.md          ← 使用指南
├── setup_mcp.ps1
│
├── Source/UEInsightMCP/
│   ├── UEInsightMCP.Build.cs
│   ├── Public/
│   │   ├── UEInsightMCPModule.h
│   │   ├── InsightBridge.h          # 命令路由（UEditorSubsystem）
│   │   ├── InsightServer.h          # TCP 服务器
│   │   ├── InsightContext.h         # Trace 状态 + 分析会话
│   │   ├── InsightLogCapture.h      # 编辑器日志环形缓冲区
│   │   └── Actions/
│   │       ├── InsightAction.h      # 基类（SEH + 线程模型）
│   │       ├── TraceControlActions.h
│   │       ├── TraceSessionActions.h
│   │       └── TraceQueryActions.h
│   └── Private/
│       ├── UEInsightMCPModule.cpp
│       ├── InsightBridge.cpp         # 注册全部 20 个 Action
│       ├── InsightServer.cpp
│       ├── InsightContext.cpp
│       ├── InsightLogCapture.cpp
│       └── Actions/
│           ├── InsightAction.cpp
│           ├── TraceControlActions.cpp   # UE4/UE5 双实现
│           ├── TraceSessionActions.cpp
│           └── TraceQueryActions.cpp     # 9 个查询 Action（~43KB）
│
└── Python/
    ├── pyproject.toml
    ├── requirements.txt
    └── ue_insight_mcp/
        ├── __init__.py
        ├── connection.py             # 持久 TCP（心跳 + 自动重连）
        ├── server_unified.py         # MCP Server（7 个工具）
        ├── registry/
        │   ├── __init__.py           # ActionRegistry + 关键词搜索引擎
        │   └── insight_actions.py    # 19 个 Action 元数据定义
        ├── resources/
        │   ├── conventions.md
        │   ├── error_codes.md
        │   └── trace_channels.md
        └── vendor/                   # 预打包的 .whl 依赖
```

---

## 构建依赖

| 模块 | 说明 |
|------|------|
| Core, CoreUObject, Engine | 基础引擎 |
| Slate, SlateCore | UI 框架 |
| UnrealEd, EditorSubsystem | 编辑器框架 |
| Json, JsonUtilities | JSON 序列化 |
| Networking, Sockets | TCP 通信 |
| TraceLog | Trace 命名空间/发射 API |
| TraceAnalysis | IAnalyzer, Trace 事件解析 |
| TraceServices | IAnalysisSession, 分析会话管理 |
| EditorScriptingUtilities | 仅 UE5（条件编译） |

---

## 路线图

### Phase 1 — Trace 控制 ✅

- 6 个 Trace 控制 Action + batch_execute
- TCP 服务器 + Python MCP 桥接
- 双引擎支持（UE5 + UE4 RDCSP）

### Phase 2 — 数据查询引擎 ✅

- 4 个会话管理 Action
- 9 个数据查询 Action（帧时间、CPU、GPU、加载时间、内存、计数器、书签、卡顿）
- 智能线程模型（查询不阻塞游戏线程）
- 分析模块自动激活修复

### Phase 3 — 智能分析 + 报告（计划中）

- `analyze.bottleneck` — AI 辅助瓶颈诊断
- `analyze.regression` — 性能回归检测
- `report.generate` — 自动生成性能报告

---

## 文档

| 文档 | 内容 |
|------|------|
| [README.md](README.md) / [README_zh.md](README_zh.md) | 项目概述、架构、Action 参考 |
| [INSTALL.md](INSTALL.md) / [INSTALL_zh.md](INSTALL_zh.md) | 安装指南 |
| [USAGE.md](USAGE.md) / [USAGE_zh.md](USAGE_zh.md) | 编辑器内使用方法、工作流、参数、问题排查 |

---

## 许可证

MIT License

## 作者

tsuyu
