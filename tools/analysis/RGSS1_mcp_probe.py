#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""直接驱动 idalib-mcp.exe: initialize -> tools/list -> 尝试打开我们的 DLL，抓 stderr

环境变量: IDALIB_MCP（必填）, RGSS1_DLL（可选）
"""
import subprocess, json, sys, threading, time, os

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.environ.get('IDALIB_MCP')
if not EXE:
    sys.exit('IDALIB_MCP is not set (path to idalib-mcp.exe)')
DLL = os.environ.get('RGSS1_DLL', os.path.join(_ROOT, 'unpacked', 'RGSS1_103J_probe.dll'))

p = subprocess.Popen([EXE, "--stdio", "--max-workers", "1"],
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.PIPE, text=True, encoding='utf-8', errors='replace')

err_lines = []
def reader():
    for ln in p.stderr:
        err_lines.append(ln.rstrip())
threading.Thread(target=reader, daemon=True).start()

def send(obj):
    p.stdin.write(json.dumps(obj) + '\n')
    p.stdin.flush()

def recv(timeout=25):
    end = time.time() + timeout
    # 同步读一行（stdin 是文本管道）
    import selectors
    line = p.stdout.readline()
    if not line:
        return None
    try:
        return json.loads(line)
    except Exception:
        return {'raw': line[:200]}

send({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"probe","version":"0"}}})
r = recv(15)
print("initialize:", json.dumps(r, ensure_ascii=False)[:300] if r else "NO REPLY")
send({"jsonrpc":"2.0","method":"notifications/initialized"})
send({"jsonrpc":"2.0","id":2,"method":"tools/list"})
r = recv(15)
if r and 'result' in r:
    tools = [(t['name'], t.get('description','')[:60]) for t in r['result'].get('tools',[])]
    print("tools:", len(tools))
    for n,d in tools[:20]: print("  -", n, '::', d)
else:
    print("tools/list:", str(r)[:300])

# 尝试打开二进制（工具名猜测：先列出再按名字含 open/binary 选）
open_tool = None
if r and 'result' in r:
    for t in r['result'].get('tools',[]):
        if 'open' in t['name'].lower() and 'binary' in (t['name']+t.get('description','')).lower():
            open_tool = t['name']; break
    if not open_tool and r['result'].get('tools'):
        open_tool = r['result']['tools'][0]['name']
if open_tool:
    send({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":open_tool,"arguments":{"path":DLL}}})
    r2 = recv(120)
    print(f"call {open_tool}:", str(r2)[:400] if r2 else "NO REPLY")

time.sleep(1)
if err_lines:
    print("=== STDERR ===")
    for ln in err_lines[-25:]: print(" ", ln)
else:
    print("(no stderr)")
p.kill()
