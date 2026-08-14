---
name: ue-trace
description: >
  Capture and deeply analyze Unreal Engine .utrace performance traces. Use when user asks to
  record/capture/start/stop a UE trace in Editor or PIE, stream trace to a Trace Server host,
  inspect an existing .utrace path, find hitches or CPU/GPU/memory/load/network bottlenecks,
  compare traces, or generate an Unreal Insights performance report. Trigger phrases include
  "抓 trace", "分析 utrace", "Unreal Insights", "性能抓取", "卡顿分析", "慢帧", "trace server",
  "录制性能", and "对比 trace".
version: 1.2.0
---

# UE Trace Capture and Deep Analysis

## Scope and truthfulness

This skill targets a UE5 project running the UEInsightMCP plugin, and its two independent MCP
layers:

- UEInsightMCP is the live Editor/PIE bridge. Its Python package is ue-insight-mcp.
- ue-trace is the offline analysis MCP for completed .utrace files.

Keep the layers separate. A live capture claim requires a successful insight_ping. If
insight_ping fails, report that real-time Editor capture/control is unavailable; do not claim
that live data can be fetched. If ue-trace is unavailable, do not pretend to analyze a trace.
When only one layer is available, state the limitation and continue only with work that layer
actually supports.

If either MCP is missing entirely, the install path is documented in the ue-trace-install skill
and in Toolchain/README.md in the UEInsightMCP repository.

MCP tool names may be namespaced by the client. Match these stable suffixes:

- Live bridge: insight_ping, insight_actions_search, insight_actions_schema,
  insight_actions_run, insight_batch, insight_logs_tail.
- Offline analysis: trace_channels, trace_overview, trace_frames, trace_frame, trace_digest,
  trace_timeline, trace_callers, trace_callees, trace_cpu_threads, trace_gpu_queues,
  trace_gpu_fences, trace_bookmark_list, trace_region_list, trace_log_messages,
  trace_counter_catalogue, trace_counter_series, trace_memory_trackers, trace_memory_tags,
  trace_memory_samples, trace_memalloc_timeline, trace_memalloc_heaps, trace_memalloc_query,
  trace_task_list, trace_task_drill, trace_asset_packages, trace_asset_requests,
  trace_asset_exports, trace_net_instances, trace_net_connections, trace_net_packets,
  trace_net_objects, trace_compare, trace_query, trace_callstack, trace_modules,
  trace_status, and trace_unload.

Use insight_actions_search/schema to discover the live action names and input shapes, then use
insight_actions_run or insight_batch. Use insight_logs_tail for bridge diagnostics.

## Safe live capture

Before controlling an Editor capture:

1. Call insight_ping.
2. Discover the schemas for trace.status, trace.channels.list, trace.start, and trace.stop.
3. Call the capture action trace.status before trace.start. Inspect managed_by_mcp,
   connection_type, and destination. Treat those fields as the source of truth for whether an
   external Trace Server stream already exists.
4. For a local file capture, call trace.start with output=file and replace_existing=true.
   The default replace_existing=false is intentionally unsafe when an external connection is
   present and can return an error.
5. By default, stop with restore_previous=true. Inspect the trace.stop response and verify
   restored_previous; do not assume the earlier stream was restored.
6. Stopping an externally owned stream requires explicit user confirmation first and
   force_external=true in the stop request.

For an external stream, duration_known=false and start_time_known=false are normal semantics.
Keep duration and start time unknown when the response says they are unknown; never infer them
from wall-clock time, filenames, or the time at which the MCP connected.

Use the absolute trace_file returned by trace.stop as the only analysis path. Before invoking
ue-trace, confirm file_exists=true and file_size_pending=false. If the response still reports
a pending size, wait and verify that two consecutive size readings are identical. Never invent
a path from a project directory, temporary directory, filename pattern, or guessed Trace Store
location.

## Channels and startup-only capture

Use the live trace.channels.list action before enabling channels. Its startup_only and
runtime_toggleable fields are authoritative for the current UE build; do not infer behavior
from a familiar channel name or preset.

memtag, memalloc, callstack, and module are startup-only channels in the memory preset. Enable
them in the Editor startup parameters when needed; do not claim that a runtime toggle succeeded.
Use this restart shape when appropriate:

~~~text
UnrealEditor.exe <uproject> -trace=default,cpu,memory -tracehost=localhost
~~~

Map requested presets to the channels actually returned by trace.channels.list. Report omitted
channels and warn that memalloc and callstack can make captures and analysis substantially
larger. Only toggle a channel at runtime when runtime_toggleable=true.

## Stable Windows offline setup

On Windows, the offline MCP must run from a stable installation rather than an npx cache. The
installer at Toolchain/Install-Toolchain.ps1 creates this layout under the user profile:

~~~text
%USERPROFILE%\.local\share\ue-trace-mcp\<version>\app\node_modules\@mtuska\ue-trace-mcp\dist\index.js
%USERPROFILE%\.local\share\ue-trace-mcp\<version>\native\windows-x64\TraceDigest.exe
~~~

The MCP entry runs `node <dist\index.js>` with `TRACE_DIGEST_BIN` pointing at that
TraceDigest.exe. tbbmalloc.dll and WinPixEventRuntime.dll must sit beside the executable, and
the installed dist must carry the Windows fixes (daemon disabled, one-shot `-out` JSON, 300s
default timeout). `Install-Toolchain.ps1 -CheckOnly` audits all of this.

Do not depend on npx or a temporary package cache for offline analysis. If analysis fails with
a missing-runtime or timeout error, re-run the installer instead of working around it. Large
allocation traces can exceed the default one-shot timeout; raise `TRACE_ONESHOT_TIMEOUT_MS`
rather than abandoning the analysis.

## Broad-to-narrow offline analysis

For a new trace, follow this order and keep each result bounded:

1. Call trace_channels, then trace_overview.
2. Call trace_frames and drill representative slow frames with trace_frame.
3. Use trace_digest for CPU and, when the GPU channel exists, GPU data; pair it with
   trace_cpu_threads, trace_gpu_queues, and trace_gpu_fences.
4. Narrow suspicious timers with trace_timeline, trace_callers, and trace_callees.
5. Only when the channel and preceding evidence justify it, enter a focused memory, load,
   task, or network investigation.
6. Correlate time with trace_bookmark_list, trace_region_list, trace_log_messages, and
   trace_counter_catalogue/trace_counter_series. Use trace_query intents when available:
   logs_around_slow_frames, events_inside_region, and frames_where_counter_exceeds.

Use these focused tool families as evidence requires them:

- Memory: trace_memory_trackers, trace_memory_tags, trace_memory_samples.
- Allocation: trace_memalloc_timeline, trace_memalloc_heaps, trace_memalloc_query; resolve
  important callstack_id values with trace_callstack and inspect trace_modules.
- Load/assets: trace_asset_packages, trace_asset_requests, trace_asset_exports.
- Tasks: trace_task_list followed by trace_task_drill for selected task ids.
- Network: trace_net_instances, trace_net_connections, trace_net_packets, trace_net_objects.
- Counters: trace_counter_catalogue followed by bounded trace_counter_series queries.

Do not call a specialized family merely because the tool exists. If trace_channels or the
tool response shows that a channel was not captured, report the missing evidence instead.
Do not infer CPU-bound versus GPU-bound behavior from a timer's presence alone; compare frame,
thread, GPU, wait, and synchronization evidence.

## Comparison and evidence

An optimization or regression conclusion requires comparable baseline and candidate captures
and a trace_compare run. Check workload, map, build, device, warmup, frame cap, channel set,
and comparable time/bookmark regions before interpreting deltas. Rank absolute impact and
percentile changes, not percentage change alone. Separate new/removed timers, frequency
changes, per-call cost, and workload-shape differences.

Without a baseline, report absolute observations only. Do not say that performance improved,
regressed, or was fixed.

For every finding, separate:

- Observation: what the tool returned, with an exact metric, unit, scope, and time window.
- Interpretation: the mechanism that the observations support, without overstating causality.
- Recommendation: a proposed change plus a validation capture and success metric.

Every finding must also include confidence (high, medium, or low) and missing evidence that
could change the interpretation. Never invent timer names, symbols, callstacks, channels,
frame indices, durations, or causal explanations.

## Chinese report template

Use this template unless the user requests another format:

~~~markdown
# UE Trace 性能分析报告

## 结论
- 观察（Observation）：...
- 解释（Interpretation）：...
- 结论置信度：高 / 中 / 低
- 缺失证据：...

## 关键证据
- Trace / frame / thread / timer / GPU / memory / load / task / network：...
- 指标、单位、范围与时间窗口：...
- baseline / candidate 与 trace_compare：...

## 时间关联
- Bookmark / region / log / counter：...
- 相关时间窗口：...

## 优先修复
1. 建议（Recommendation）：...
   - 预期影响：...
   - 验证指标：...

## 验证方案
- 使用相同场景、构建、设备、预热、帧率限制与 channel 重新抓取：...
- 对 baseline 与 candidate 执行 trace_compare：...
- 成功标准：...

## 限制
- 缺失 channel、符号、baseline、可比条件或文件稳定性证据：...
- 不确定性与可能改变结论的证据：...
~~~

## Cleanup

For a large trace, call trace_unload after analysis when no further query is expected, or before
loading another large trace. Never proactively delete a .utrace file. Delete or overwrite
trace artifacts only when the user explicitly requests it and confirms the exact path.
