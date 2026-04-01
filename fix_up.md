# UEInsightMCP Fix-Up Log

---

## Fix #1: 线程模型设计错误 — session/query actions 阻塞游戏线程

**日期**: 2026-03-27  
**严重度**: Critical — Phase 2 所有 query actions 超时不可用

### 症状

Phase 2 端到端测试结果：

| Action | 结果 | 说明 |
|--------|------|------|
| session.list | ✅ | |
| session.open | ✅ | 0.7s 解析完成 |
| session.info | ✅ | 2379 帧 / 93 线程 / 1957 counters |
| query.frame_times | ❌ 超时 | |
| query.counters | ❌ 超时 | |
| query.bookmarks | ❌ 超时 | |
| trace.status | ❌ 超时 | 被堆积任务波及 |

session.open/info 能成功是因为游戏线程队列前面没有堆积任务。query actions 开始超时后，Python MCP 端放弃等待，但 C++ 端的 `AsyncTask(GameThread)` 仍在队列中排队。后续所有请求（包括 Phase 1 的 trace.status）全部排在阻塞任务后面，导致雪崩式超时。

### 根因

从 UEEditorMCP 复制架构时，所有 action 无差别通过 `AsyncTask(ENamedThreads::GameThread)` 分发到游戏线程执行。

- UEEditorMCP 操作编辑器对象（Actor / Blueprint）→ 必须游戏线程 ✅
- UEInsightMCP 的 session/query actions 只读离线 .utrace 数据 → **不需要游戏线程** ❌

原生 Unreal Insights 的分析功能是独立进程（UnrealInsights.exe），完全不占编辑器游戏线程。我们把离线分析硬绑在游戏线程上属于设计错误。

### 修复

引入 `RequiresGameThread()` 虚方法，让 action 自己声明是否需要游戏线程，Server 根据返回值选择执行路径。

**修改的文件（5 个）：**

#### 1. `InsightAction.h`
```cpp
// 基类 — public 虚方法，默认 true（安全）
class FInsightAction {
public:
    virtual bool RequiresGameThread() const { return true; }
};

// Session actions — override false
class FTraceSessionAction : public FInsightAction {
    virtual bool RequiresGameThread() const override { return false; }
};

// Query actions — override false
class FTraceQueryAction : public FInsightAction {
    virtual bool RequiresGameThread() const override { return false; }
};
```

#### 2. `InsightBridge.h` / `InsightBridge.cpp`
```cpp
// 查询 action 是否需要游戏线程
bool ShouldRunOnGameThread(const FString& CommandType) const;

// 在调用线程直接执行（不走游戏线程 dispatch）
TSharedPtr<FJsonObject> ExecuteCommandDirect(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params);
```

#### 3. `InsightServer.h` / `InsightServer.cpp`
```cpp
// Run() 中分流
if (Bridge && !Bridge->ShouldRunOnGameThread(CommandType))
{
    // 离线分析 → TCP 工作线程直接执行
    FString Response = ExecuteDirectly(CommandType, Params);
    SendResponse(Response);
}
else
{
    // Trace 控制 → 游戏线程 dispatch
    FString Response = ExecuteOnGameThread(CommandType, Params);
    SendResponse(Response);
}
```

### 线程分配总览

| Action 类别 | 执行线程 | 原因 |
|-------------|---------|------|
| trace.start/stop/status | 游戏线程 | 操作运行时 Trace 系统 |
| trace.channels.list/toggle | 游戏线程 | 操作运行时 Trace 系统 |
| trace.bookmark.add | 游戏线程 | 写入运行时 Trace 流 |
| batch_execute | 游戏线程 | 默认（内含 trace 命令） |
| session.list/open/info/close | TCP 工作线程 | 只读磁盘 .utrace 文件 |
| query.* (所有 9 个) | TCP 工作线程 | 只读分析数据 |

---

## Fix #2: Doxygen 注释中 `*/` 导致编译错误

**日期**: 2026-03-27  
**严重度**: Build-breaking

### 症状

MSVC 报大量语法错误：
```
InsightAction.h(46): Error C2143: 语法错误: 缺少";"(在".*"的前面)
InsightAction.h(49): Error C2334: "{"的前面有意外标记；跳过明显的函数体
InsightAction.h(105): Error C3668: "FTraceSessionAction::RequiresGameThread": 
    包含重写说明符"override"的方法没有重写任何基类方法
InsightBridge.h(52): Error C2143: 语法错误: 缺少";"(在".*"的前面)
```

### 根因

Fix #1 中写的 Doxygen 注释包含 `session.*/query.*`：

```cpp
/**
 * Override to false for offline analysis actions (session.*/query.*) that
 * only read .utrace files...
 */
```

C++ 编译器看到 `session.*/` 时，把 `*/` 当作多行注释 `/** ... */` 的结束符。之后 `query.*) that` 变成裸代码，引发连锁语法错误。

由于基类 `RequiresGameThread()` 声明被注释截断，编译器看不到它，子类的 `override` 自然报 C3668（没有基类方法可重写）。

### 修复

将注释中的通配符表示改为不含 `*/` 的写法：

```diff
- * Override to false for offline analysis actions (session.*/query.*) that
+ * Override to false for offline analysis actions (session / query)
+ * that only read .utrace files and never touch editor/engine state.
```

同样修复了 `InsightBridge.h` 中的相同注释。

### 教训

在 `/* */` 或 `/** */` 注释块内，永远不要写包含 `*/` 的文本（即使它是通配符 `.*` 后跟 `/`）。如果需要表示通配符模式，用反引号、转义或换种写法。

---

## Fix #3: `inf` 导致无效 JSON — query.frame_times / query.hitches 返回数据无法解析

**日期**: 2026-03-27  
**严重度**: Critical — 两个 query action 完全不可用，且导致 Python MCP bridge 连接中断

### 症状

通过 MCP 调用 `query.frame_times` 和 `query.hitches` 时报 `MCP error -32001: Request timed out`。之后所有 MCP 工具（包括 ping）都不通。

直接 TCP 测试发现 C++ 端实际在 2ms 内就返回了，但返回的 JSON 无法解析：

```json
{
    "total_time_seconds": inf,
    "avg_ms": inf,
    "max_ms": inf,
    "worst_frames": [{"duration_ms": inf, "fps": 0}]
}
```

`inf` 不是合法的 JSON 值。Python `json.loads()` 在遇到 `inf` 时抛 `JSONDecodeError`。

同时验证了其他 query actions 正常：
- `query.counters` ✅ (1ms, 4/1957 counters matched)
- `query.bookmarks` ✅ (0ms, 21 bookmarks)

### 根因

Trace 录制的最后一帧（Frame #2378）的 `EndTime` 为 0 或无穷大（trace 结束时最后一帧可能未完成）。

计算 `Duration = Frame.EndTime - Frame.StartTime` 得到 `inf`，然后：
- `TotalTime += inf` → inf
- `AvgTime = inf / N` → inf  
- `MaxTime = max(x, inf)` → inf

UE 的 `FJsonSerializer` 把 `double INFINITY` 序列化为字面量 `inf`（非标准 JSON），Python 端无法解析。

### 修复

在 `query.frame_times` 和 `query.hitches` 的帧遍历中增加有效性检查：

```cpp
// Before (broken):
if (Duration > 0.0) { ... }

// After (fixed):
if (Duration > 0.0 && FMath::IsFinite(Duration) && Duration < 60.0) { ... }
```

过滤条件：
1. `Duration > 0.0` — 排除零或负值
2. `FMath::IsFinite(Duration)` — 排除 inf 和 NaN
3. `Duration < 60.0` — 排除不合理的超长帧（>60秒，肯定是数据异常）

**修改文件：** `TraceQueryActions.cpp`（query.frame_times + query.hitches 两处）

### 教训

1. **Trace 数据边界条件**：最后一帧经常是不完整的，EndTime 可能未写入
2. **UE JSON 序列化的 inf 陷阱**：`FJsonSerializer` 不会把 `inf` 转成 `null` 或跳过，而是直接写 `inf` 字面量
3. **Always validate floating point from external data**：任何从 trace/profiler 读取的浮点数都应该做 `IsFinite()` 检查

---

## Fix #4: `inf` 扩散 — `SessionEnd = LastFrame->EndTime` 在 cpu_threads / gpu_timing / loadtime_deps 中同样中招

**日期**: 2026-03-27  
**严重度**: Critical — 同 Fix #3 根因，影响另外 3 个 query action

### 症状

`query.cpu_threads` 返回 `"session_end": inf`，导致 JSON 无法解析。  
`query.gpu_timing` 和 `query.loadtime_deps` 包含相同的 `SessionEnd = Last->EndTime` 代码。

### 根因

与 Fix #3 完全相同：trace 最后一帧的 `EndTime` 为 inf。  
三处都用 `LastFrame->EndTime` 或 `Last->EndTime` 直接赋值 `SessionEnd`，不做 `IsFinite()` 检查。

更严重的是，`SessionEnd` 还被传给 `FCreateAggreationParams::IntervalEnd`，inf 区间可能导致 aggregation 行为异常（虽然本次测试 cpu_threads 数据本身正确，只是 JSON 无效）。

### 修复

三处全部改为倒序遍历找第一个有效 EndTime：

```cpp
// Before:
if (Last) SessionEnd = Last->EndTime;

// After:
for (int64 i = (int64)FrameCount - 1; i >= 0; --i)
{
    const TraceServices::FFrame* F = FrameProvider.GetFrame(TraceFrameType_Game, i);
    if (F && FMath::IsFinite(F->EndTime) && F->EndTime > SessionStart)
    {
        SessionEnd = F->EndTime;
        break;
    }
}
```

**修改文件：** `TraceQueryActions.cpp`（cpu_threads + gpu_timing + loadtime_deps 三处）

### 教训

1. **同一个 bug pattern 必须全局排查** — Fix #3 修了 frame_times/hitches 但没检查其他 action 里相同的 `LastFrame->EndTime` 代码
2. **复制粘贴的代码 = 复制粘贴的 bug** — 四处获取 SessionEnd 的代码几乎一样，应该抽成共用函数

---

## Fix #5: 全面 inf/NaN 防御 — 所有 query action 的所有浮点输出

**日期**: 2026-03-27  
**严重度**: High — 系统性修复，杜绝所有 `inf` 序列化为无效 JSON 的可能

### 问题

Fix #3 和 #4 只修了"已经被 inf 打中的地方"（frame EndTime → SessionEnd / Duration）。  
但实际上 **trace 数据中任何浮点值都可能是 inf/NaN**：
- Aggregation 输出的 `TotalInclusiveTime / ExclusiveTime / AverageInclusiveTime / MaxInclusiveTime`
- LoadRequest 的 `EndTime`（未完成加载的 request）
- PackageDetails 的 `MainThreadTime / AsyncLoadingThreadTime`
- Counter 的 `Value`（float counter）和 `Time`
- Bookmark 的 `Time`
- Memory tag 的 `Sample.Value`

### 修复方案

**1. 基类工具函数**（`InsightAction.h` → `FTraceQueryAction`）：

```cpp
// Sanitize float for JSON output — returns Fallback for inf/NaN
static double SanitizeFloat(double Value, double Fallback = 0.0);

// Returns true if Value is finite and safe for calculations
static bool IsValidFloat(double Value);

// SetNumberField with auto-sanitization
static void SafeSetNumber(const TSharedPtr<FJsonObject>& Obj, const FString& Key, double Value, double Fallback = 0.0);

// Shared: get valid session start/end from FrameProvider (handles inf EndTime)
static void GetValidSessionTimeRange(const IFrameProvider&, ETraceFrameType, double& OutStart, double& OutEnd);
```

**2. 逐 action 修复**：

| Action | 修复点 | 方式 |
|--------|--------|------|
| **cpu_threads** | SessionEnd 计算 | `GetValidSessionTimeRange()` |
| **cpu_threads** | Aggregation Row values | `IsValidFloat()` filter + skip |
| **cpu_threads** | JSON session_start/end | `SafeSetNumber()` |
| **gpu_timing** | SessionEnd 计算 | `GetValidSessionTimeRange()` |
| **gpu_timing** | Aggregation Row values | `IsValidFloat()` filter + skip |
| **loadtime** | `Row->EndTime - Row->StartTime` | `IsValidFloat()` + clamp to 0 |
| **loadtime** | `Row->StartTime` | `IsValidFloat()` check |
| **loadtime_deps** | SessionEnd 计算 | `GetValidSessionTimeRange()` |
| **loadtime_deps** | `MainThreadTime / AsyncTime` | `IsValidFloat()` check |
| **counters** | Float counter `Value` | `FMath::IsFinite()` check |
| **counters** | `Time` | `FMath::IsFinite()` check |
| **bookmarks** | `Bookmark.Time` | `FMath::IsFinite()` check |
| **memory** | `Sample.Value` | `FMath::IsFinite()` check |

**修改文件：**
- `InsightAction.h` — 新增 4 个工具方法
- `TraceQueryActions.cpp` — 所有 9 个 query action 的浮点输出

### 教训

1. **防御性编程要彻底** — 不能只修"已知出问题的地方"，应该从数据源头到 JSON 输出全链路做 sanitize
2. **共用函数 > 复制粘贴** — `GetValidSessionTimeRange()` 替代了 4 处几乎一样的倒序遍历代码
3. **原则：凡是写入 JSON 的 float，都要过 IsFinite**
