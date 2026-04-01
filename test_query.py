"""Quick TCP test for query.frame_times"""
import socket, json, struct, time

def send_recv(s, cmd_type, params=None):
    cmd = {"type": cmd_type}
    if params:
        cmd["params"] = params
    data = json.dumps(cmd).encode()
    s.sendall(struct.pack('>I', len(data)))
    s.sendall(data)
    
    hdr = b''
    while len(hdr) < 4:
        hdr += s.recv(4 - len(hdr))
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
        print(f"!!! JSON DECODE ERROR at char {e.pos}: {e.msg}")
        print(f"!!! Response length header: {length}, actual body bytes: {len(body)}")
        print(f"!!! First 1000 chars:\n{text[:1000]}")
        pos = e.pos
        print(f"!!! Around error position ({pos}):\n...{text[max(0,pos-80):pos+80]}...")
        return {"success": False, "error": f"Invalid JSON at char {e.pos}: {e.msg}", "_raw_preview": text[:500]}

s = socket.socket()
s.settimeout(120)
s.connect(('127.0.0.1', 55559))
print("Connected to C++ server")

# First: list trace files and open the first one
print("\n=== session.list ===")
resp = send_recv(s, "session.list", {})
files = resp.get('files', [])
if files:
    trace_path = files[0]['path']
    print(f"Found: {trace_path} ({files[0].get('size_mb', 0):.1f} MB)")
    print("\n=== session.open ===")
    t0 = time.time()
    resp = send_recv(s, "session.open", {"file": trace_path})
    elapsed = time.time() - t0
    print(f"Elapsed: {elapsed*1000:.0f}ms")
    print(f"Success: {resp.get('success')}")
    if resp.get('success'):
        print(f"Providers: {resp.get('available_providers', [])}")
else:
    print("No trace files found! Record a trace first.")
    s.close()
    exit(1)

# Test query.frame_times
print("\n=== query.frame_times ===")
t0 = time.time()
resp = send_recv(s, "query.frame_times", {})
elapsed = time.time() - t0
print(f"Elapsed: {elapsed*1000:.0f}ms")
print(f"Success: {resp.get('success')}")
summary = resp.get('summary', {})
if summary:
    print(f"Frames: {summary.get('total_frames')}")
    print(f"Avg: {summary.get('avg_ms', 0):.2f}ms ({summary.get('avg_fps', 0):.1f} fps)")
    print(f"P95: {summary.get('p95_ms', 0):.2f}ms")
    print(f"P99: {summary.get('p99_ms', 0):.2f}ms")
    print(f"Min: {summary.get('min_ms', 0):.2f}ms | Max: {summary.get('max_ms', 0):.2f}ms")
dist = resp.get('distribution', [])
if dist:
    print("\nFPS Distribution:")
    for b in dist:
        print(f"  {b['label']}: {b['count']} ({b['percentage']:.1f}%)")
worst = resp.get('worst_frames', [])
if worst:
    print(f"\nWorst {len(worst)} frames:")
    for w in worst[:5]:
        print(f"  Frame {int(w['frame_index'])}: {w['duration_ms']:.1f}ms ({w['fps']:.1f} fps)")
else:
    print(f"Error: {resp.get('error')}")

# Test query.counters with filter
print("\n=== query.counters (filter=FPS) ===")
t0 = time.time()
resp = send_recv(s, "query.counters", {"filter": "FPS"})
elapsed = time.time() - t0
print(f"Elapsed: {elapsed*1000:.0f}ms")
print(f"Success: {resp.get('success')}")
print(f"Matched: {resp.get('matched_counters', 0)} / {resp.get('total_counters', 0)}")
for c in resp.get('counters', [])[:5]:
    print(f"  [{c.get('id')}] {c.get('name')} ({c.get('group')})")

# Test query.bookmarks
print("\n=== query.bookmarks ===")
t0 = time.time()
resp = send_recv(s, "query.bookmarks", {})
elapsed = time.time() - t0
print(f"Elapsed: {elapsed*1000:.0f}ms")
print(f"Success: {resp.get('success')}")
print(f"Total: {resp.get('total_bookmarks', 0)}, Matched: {resp.get('matched_bookmarks', 0)}")
for b in resp.get('bookmarks', [])[:5]:
    print(f"  [{b.get('time_ms', 0):.0f}ms] {b.get('text')}")

# Test query.hitches
print("\n=== query.hitches (threshold=33.33ms) ===")
t0 = time.time()
resp = send_recv(s, "query.hitches", {"threshold_ms": 33.33})
elapsed = time.time() - t0
print(f"Elapsed: {elapsed*1000:.0f}ms")
print(f"Success: {resp.get('success')}")
print(f"Hitches: {resp.get('hitch_count', 0)} / {resp.get('total_frames', 0)} frames ({resp.get('hitch_percentage', 0):.1f}%)")
summary = resp.get('summary', {})
if summary:
    print(f"Worst: {summary.get('worst_hitch_ms', 0):.1f}ms, Avg: {summary.get('avg_hitch_ms', 0):.1f}ms")

# Close
send_recv(s, "close")
s.close()
print("\n=== ALL QUERY TESTS DONE ===")
