"""Complete end-to-end TCP test for ALL Phase 2 actions"""
import socket, json, struct, time, sys

def send_recv(s, cmd_type, params=None):
    cmd = {"type": cmd_type}
    if params:
        cmd["params"] = params
    data = json.dumps(cmd).encode()
    s.sendall(struct.pack('>I', len(data)))
    s.sendall(data)
    
    hdr = b''
    while len(hdr) < 4:
        chunk = s.recv(4 - len(hdr))
        if not chunk:
            raise ConnectionError("Connection closed reading header")
        hdr += chunk
    length = struct.unpack('>I', hdr)[0]
    
    body = b''
    while len(body) < length:
        chunk = s.recv(min(65536, length - len(body)))
        if not chunk:
            break
        body += chunk
    text = body.decode('utf-8', errors='replace')
    try:
        return json.loads(text)
    except json.JSONDecodeError as e:
        print(f"  !!! JSON ERROR at char {e.pos}: {e.msg}")
        print(f"  !!! Around error: ...{text[max(0,e.pos-100):e.pos+100]}...")
        return {"success": False, "error": f"Invalid JSON at {e.pos}: {e.msg}"}

def test(s, label, action_id, params=None):
    print(f"\n{'='*60}")
    print(f"  {label}")
    print(f"{'='*60}")
    t0 = time.time()
    resp = send_recv(s, action_id, params or {})
    elapsed = (time.time() - t0) * 1000
    ok = resp.get('success', False)
    status = "PASS" if ok else "FAIL"
    print(f"  [{status}] {elapsed:.0f}ms")
    if not ok:
        print(f"  Error: {resp.get('error', 'unknown')}")
    return ok, elapsed, resp

# Connect
s = socket.socket()
s.settimeout(120)
s.connect(('127.0.0.1', 55559))
print("Connected to C++ server on port 55559\n")

results = []

# --- Session actions ---
ok, ms, resp = test(s, "session.list", "session.list")
results.append(("session.list", ok, ms))
if ok and resp.get('files'):
    trace_file = resp['files'][0]['path']
    print(f"  File: {trace_file}")
    print(f"  Size: {resp['files'][0].get('size_mb', 0):.1f} MB")
else:
    print("  No trace files found!")
    sys.exit(1)

ok, ms, resp = test(s, "session.open", "session.open", {"file": trace_file})
results.append(("session.open", ok, ms))
if ok:
    print(f"  Providers: {resp.get('available_providers', [])}")
    print(f"  Analyze time: {resp.get('analyze_time_seconds', 0):.2f}s")

ok, ms, resp = test(s, "session.info", "session.info")
results.append(("session.info", ok, ms))
if ok:
    frames = resp.get('frames', {})
    print(f"  Frames: {frames.get('game_frame_count', 0)}")
    print(f"  Threads: {resp.get('thread_info', {}).get('count', 0)}")
    print(f"  Counters: {resp.get('counter_count', 0)}")
    print(f"  Timing: {resp.get('has_timing_profiler')}")
    print(f"  LoadTime: {resp.get('has_loadtime_profiler')}")

# --- Query actions ---
ok, ms, resp = test(s, "query.frame_times", "query.frame_times")
results.append(("query.frame_times", ok, ms))
if ok:
    s2 = resp.get('summary', {})
    print(f"  Frames: {s2.get('total_frames')}")
    print(f"  Avg: {s2.get('avg_ms', 0):.2f}ms ({s2.get('avg_fps', 0):.1f} fps)")
    print(f"  P95: {s2.get('p95_ms', 0):.2f}ms | P99: {s2.get('p99_ms', 0):.2f}ms")
    worst = resp.get('worst_frames', [])
    if worst:
        print(f"  Worst: Frame#{int(worst[0]['frame_index'])} = {worst[0]['duration_ms']:.1f}ms")

ok, ms, resp = test(s, "query.cpu_threads", "query.cpu_threads")
results.append(("query.cpu_threads", ok, ms))
if ok:
    threads = resp.get('threads', [])
    print(f"  Thread count: {len(threads)}")
    for t in threads[:5]:
        print(f"    {t.get('name', '?')}: {t.get('total_time_ms', 0):.1f}ms, top={t.get('top_timers', [{}])[0].get('name', '?') if t.get('top_timers') else 'none'}")

ok, ms, resp = test(s, "query.gpu_timing", "query.gpu_timing")
results.append(("query.gpu_timing", ok, ms))
if ok:
    s2 = resp.get('summary', {})
    print(f"  Total GPU time: {s2.get('total_gpu_ms', 0):.1f}ms")
    top = resp.get('top_passes', [])
    for p in top[:5]:
        print(f"    {p.get('name', '?')}: {p.get('total_ms', 0):.1f}ms ({p.get('percentage', 0):.1f}%)")

ok, ms, resp = test(s, "query.loadtime", "query.loadtime")
results.append(("query.loadtime", ok, ms))
if ok:
    assets = resp.get('assets', [])
    print(f"  Assets loaded: {resp.get('total_assets', 0)}")
    for a in assets[:5]:
        print(f"    {a.get('name', '?')}: {a.get('load_time_ms', 0):.1f}ms")

ok, ms, resp = test(s, "query.memory", "query.memory")
results.append(("query.memory", ok, ms))
if ok:
    tags = resp.get('tags', [])
    print(f"  Memory tags: {len(tags)}")
    for t in tags[:5]:
        print(f"    {t.get('name', '?')}: {t.get('current_mb', 0):.1f} MB")

ok, ms, resp = test(s, "query.counters (filter=FPS)", "query.counters", {"filter": "FPS"})
results.append(("query.counters", ok, ms))
if ok:
    print(f"  Matched: {resp.get('matched_counters', 0)} / {resp.get('total_counters', 0)}")

ok, ms, resp = test(s, "query.bookmarks", "query.bookmarks")
results.append(("query.bookmarks", ok, ms))
if ok:
    print(f"  Bookmarks: {resp.get('total_bookmarks', 0)}")
    for b in resp.get('bookmarks', [])[:3]:
        print(f"    [{b.get('time_ms', 0):.0f}ms] {b.get('text')}")

ok, ms, resp = test(s, "query.hitches (>33.33ms)", "query.hitches", {"threshold_ms": 33.33})
results.append(("query.hitches", ok, ms))
if ok:
    print(f"  Hitches: {resp.get('hitch_count', 0)} / {resp.get('total_frames', 0)} ({resp.get('hitch_percentage', 0):.1f}%)")
    s2 = resp.get('summary', {})
    print(f"  Worst: {s2.get('worst_hitch_ms', 0):.1f}ms | Avg: {s2.get('avg_hitch_ms', 0):.1f}ms")

# --- Session close ---
ok, ms, resp = test(s, "session.close", "session.close")
results.append(("session.close", ok, ms))

s.close()

# --- Summary ---
print(f"\n{'='*60}")
print(f"  SUMMARY")
print(f"{'='*60}")
passed = sum(1 for _, ok, _ in results if ok)
failed = sum(1 for _, ok, _ in results if not ok)
for name, ok, ms in results:
    status = "PASS" if ok else "FAIL"
    print(f"  {status}  {name:25s}  {ms:8.1f}ms")
print(f"\n  Total: {passed} passed, {failed} failed out of {len(results)}")
