"""Quick TCP test for UEInsightMCP InsightServer on port 55559"""
import socket
import json
import struct
import sys
import os

os.environ["PYTHONIOENCODING"] = "utf-8"
if sys.stdout.encoding != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")

HOST = "127.0.0.1"
PORT = 55559

def send_recv(sock, payload: dict) -> dict:
    """Send a length-prefixed JSON message and receive the response."""
    msg = json.dumps(payload).encode("utf-8")
    # Send: 4-byte big-endian length + body
    sock.sendall(struct.pack(">I", len(msg)) + msg)
    
    # Receive: 4-byte length header
    hdr = b""
    while len(hdr) < 4:
        chunk = sock.recv(4 - len(hdr))
        if not chunk:
            raise ConnectionError("Connection closed while reading header")
        hdr += chunk
    
    length = struct.unpack(">I", hdr)[0]
    print(f"  [recv] expecting {length} bytes")
    
    # Receive body
    data = b""
    while len(data) < length:
        chunk = sock.recv(min(length - len(data), 4096))
        if not chunk:
            raise ConnectionError("Connection closed while reading body")
        data += chunk
    
    return json.loads(data.decode("utf-8"))

def main():
    print(f"Connecting to {HOST}:{PORT}...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    
    try:
        s.connect((HOST, PORT))
        print("Connected!\n")
        
        # Test 1: ping
        print("=== Test 1: ping ===")
        resp = send_recv(s, {"type": "ping"})
        print(f"  Response: {json.dumps(resp, indent=2)}")
        assert resp.get("status") == "success", f"Unexpected status: {resp}"
        print("  PASS ✓\n")
        
        # Test 2: get_context
        print("=== Test 2: get_context ===")
        resp = send_recv(s, {"type": "get_context"})
        print(f"  Response: {json.dumps(resp, indent=2)}")
        assert resp.get("status") == "success", f"Unexpected status: {resp}"
        print("  PASS ✓\n")
        
        # Test 3: trace.status
        print("=== Test 3: trace.status ===")
        resp = send_recv(s, {"type": "trace.status", "params": {}})
        print(f"  Response: {json.dumps(resp, indent=2)}")
        print(f"  Status: {'PASS ✓' if resp.get('status') == 'success' else 'FAIL ✗'}\n")
        
        # Test 4: trace.channels.list
        print("=== Test 4: trace.channels.list ===")
        resp = send_recv(s, {"type": "trace.channels.list", "params": {}})
        print(f"  Response: {json.dumps(resp, indent=2)[:500]}...")
        print(f"  Status: {'PASS ✓' if resp.get('status') == 'success' else 'FAIL ✗'}\n")
        
        # Test 5: trace.start (file mode, short recording)
        print("=== Test 5: trace.start (file mode) ===")
        resp = send_recv(s, {
            "type": "trace.start",
            "params": {
                "mode": "file",
                "channels": "cpu,frame,bookmark"
            }
        })
        print(f"  Response: {json.dumps(resp, indent=2)}")
        print(f"  Status: {'PASS ✓' if resp.get('status') == 'success' else 'FAIL ✗'}\n")
        
        # Test 6: trace.bookmark.add
        print("=== Test 6: trace.bookmark.add ===")
        resp = send_recv(s, {
            "type": "trace.bookmark.add",
            "params": {
                "label": "MCP_TEST_BOOKMARK"
            }
        })
        print(f"  Response: {json.dumps(resp, indent=2)}")
        print(f"  Status: {'PASS ✓' if resp.get('status') == 'success' else 'FAIL ✗'}\n")
        
        # Test 7: trace.status (should be tracing now)
        print("=== Test 7: trace.status (during recording) ===")
        resp = send_recv(s, {"type": "trace.status", "params": {}})
        print(f"  Response: {json.dumps(resp, indent=2)}")
        print(f"  Status: {'PASS ✓' if resp.get('status') == 'success' else 'FAIL ✗'}\n")
        
        # Test 8: trace.stop
        print("=== Test 8: trace.stop ===")
        resp = send_recv(s, {"type": "trace.stop", "params": {}})
        print(f"  Response: {json.dumps(resp, indent=2)}")
        print(f"  Status: {'PASS ✓' if resp.get('status') == 'success' else 'FAIL ✗'}\n")
        
        # Test 9: close
        print("=== Test 9: close ===")
        resp = send_recv(s, {"type": "close"})
        print(f"  Response: {json.dumps(resp, indent=2)}")
        print("  PASS ✓\n")
        
        print("=" * 50)
        print("ALL TESTS COMPLETED!")
        
    except Exception as e:
        print(f"\nERROR: {type(e).__name__}: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
    finally:
        s.close()

if __name__ == "__main__":
    main()
