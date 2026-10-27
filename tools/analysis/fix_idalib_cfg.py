import json, os, sys

# 用法: py fix_idalib_cfg.py <IDA 安装目录>
# 或设 IDA_INSTALL_DIR 环境变量。
ida_dir = sys.argv[1] if len(sys.argv) > 1 else os.environ.get('IDA_INSTALL_DIR')
if not ida_dir:
    sys.exit('usage: fix_idalib_cfg.py <ida-install-dir>   (or set IDA_INSTALL_DIR)')

p = os.path.join(os.environ['APPDATA'], 'Hex-Rays', 'IDA Pro', 'ida-config.json')
cfg = json.load(open(p))
old = cfg['Paths']['ida-install-dir']
cfg['Paths']['ida-install-dir'] = ida_dir
json.dump(cfg, open(p, 'w'), indent=4)
print('changed:', old, '->', cfg['Paths']['ida-install-dir'])
