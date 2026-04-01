"""Quick TCP test for InsightMCP C++ server (port 55559)."""
import socket, json, struct, sys

def send_cmd(cmd_type, params=None):
    s = socket.socket()
    s.settimeout(30)
    s.connect(('127.0.0.1', 55559))
    msg = json.dumps({'type': cmd_type, 'params': params or {}}).encode('utf-8')
    s.sendall(struct.pack('>I', len(msg)) + msg)
    hdr = s.recv(4)
    ln = struct.unpack('>I', hdr)[0]
    data = b''
    while len(data) < ln:
        chunk = s.recv(min(ln - len(data), 65536))
        if not chunk:
            break
        data += chunk
    s.close()
    return json.loads(data.decode('utf-8'))

if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'trace.status'
    # Parse key=value pairs into params dict
    params = {}
    for arg in sys.argv[2:]:
        if '=' in arg:
            k, v = arg.split('=', 1)
            # Try to parse as JSON value (number, bool, etc)
            try:
                params[k] = json.loads(v)
            except (json.JSONDecodeError, ValueError):
                params[k] = v
        else:
            # Try as raw JSON
            try:
                params = json.loads(arg)
            except:
                pass
    result = send_cmd(cmd, params)
    print(json.dumps(result, indent=2, ensure_ascii=False))
