#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""RGSS1_ida_client.py — 直接驱动 idalib-mcp.exe（绕过 REA node 层）
用法:
  py RGSS1_ida_client.py open            # 打开 DLL（首次全量分析，慢）并 idb_save
  py RGSS1_ida_client.py funcs [N]       # list_funcs 前 N 个
  py RGSS1_ida_client.py decomp ADDR...  # 反编译指定地址(0x10003b90 等)
  py RGSS1_ida_client.py health

环境变量:
  IDALIB_MCP  idalib-mcp.exe 路径（必填）
  RGSS1_DLL   待分析的 DLL 路径（默认 <repo>/unpacked/RGSS1_103J_probe.dll）
"""
import subprocess, json, sys, threading, time, os

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.environ.get('IDALIB_MCP')
if not EXE:
    sys.exit('IDALIB_MCP is not set (path to idalib-mcp.exe)')
DLL = os.environ.get('RGSS1_DLL', os.path.join(_ROOT, 'unpacked', 'RGSS1_103J_probe.dll'))

class Client:
    def __init__(self):
        self.p = subprocess.Popen([EXE, "--stdio", "--max-workers", "1"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding='utf-8', errors='replace')
        self.err = []
        threading.Thread(target=self._rd, daemon=True).start()
        self._id = 0
    def _rd(self):
        for ln in self.p.stderr:
            self.err.append(ln.rstrip())
    def send(self, obj):
        self.p.stdin.write(json.dumps(obj) + '\n'); self.p.stdin.flush()
    def call(self, method, params=None, timeout=180, note=''):
        self._id += 1
        i = self._id
        self.send({"jsonrpc":"2.0","id":i,"method":method,"params":params or {}})
        if method != 'notifications/initialized':
            pass
        line = self.p.stdout.readline()
        # blocking read; workers may be slow — rely on server, harness timeout guards
        if not line:
            return None
        try:
            return json.loads(line)
        except Exception:
            return {'raw': line[:300]}
    def tool(self, name, args=None, timeout=180):
        self._id += 1
        i = self._id
        self.send({"jsonrpc":"2.0","id":i,"method":"tools/call","params":{"name":name,"arguments":args or {}}})
        line = self.p.stdout.readline()
        if not line:
            return None
        try:
            r = json.loads(line)
        except Exception:
            return {'raw': line[:300]}
        if r and 'result' in r:
            txt = ''.join(c.get('text','') for c in r['result'].get('content',[]))
            return {'isError': r['result'].get('isError', False), 'text': txt}
        return r

def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'health'
    c = Client()
    c.call("initialize", {"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"rgss1","version":"0"}})
    c.send({"jsonrpc":"2.0","method":"notifications/initialized"})
    # 打开 idb（首次触发全量分析 + Hex-Rays）
    r = c.tool('idb_open', {'input_path': DLL, 'run_auto_analysis': True,
                            'build_caches': True, 'init_hexrays': True}, timeout=180)
    print('idb_open:', str(r)[:160])
    if r and r.get('isError'):
        print('open failed'); return
    sid = None
    try:
        sid = json.loads(r['text'])['session']['session_id']
    except Exception:
        pass
    def T(name, args=None, timeout=180):
        a = dict(args or {}); a['database'] = sid
        return c.tool(name, a, timeout)
    if cmd == 'funcs':
        n = int(sys.argv[2]) if len(sys.argv) > 2 else 40
        r = T('list_funcs', {'offset': 0, 'count': n})
        print(r['text'][:5000] if r else 'NO REPLY')
    elif cmd == 'decomp':
        for a in sys.argv[2:]:
            r = T('decompile', {'addr': a})
            print(f'=== {a} ===')
            print((r['text'][:7000] if r else 'NO REPLY'))
    elif cmd == 'search':
        # search_text <pattern>
        r = T('search_text', {'pattern': sys.argv[2]})
        print(r['text'][:3000] if r else 'NO REPLY')
    if cmd in ('funcs', 'decomp'):
        r = T('idb_save', {})
        print('idb_save:', str(r)[:120])
    print('--- stderr tail ---')
    for ln in c.err[-6:]: print(' ', ln)

if __name__ == '__main__':
    main()
