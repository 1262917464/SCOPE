# third_party — 外部源码获取说明

本目录的第三方源码**不进版本库**（共 81MB，且是公开可重新取得的上游代码）。
按下面的方法放回本目录，即可复现 M3 里程碑的 Ruby 集成。

## 需要的两个包

| 目录 | 用途 | 官方来源 |
|------|------|----------|
| `ruby-1.8.1/` | **主力**。编译进 RGSS202E 重建版 DLL，充当解释器层 | <https://cache.ruby-lang.org/pub/ruby/1.8/ruby-1.8.1.tar.gz> |
| `ruby-1.9.2-p0/` | 参照物。用于比对不同 Ruby 版本的 C API 差异 | <https://cache.ruby-lang.org/pub/ruby/1.9/ruby-1.9.2-p0.tar.gz> |

## 校验值（务必核对）

```
d0f1e19fc7a14233ed0e1335867f7a7ddb18b85ab4c58795c5fc5a562710de4e  ruby-1.8.1.tar.gz
8c0c4e261a921b5c406bf9e76ac23bf3c915651534e9d1b9e8c5d0bee4a7285c  ruby-1.9.2-p0.tar.gz
```

## 放回步骤

```bash
cd third_party
curl -Lo ruby-1.8.1.tar.gz    https://cache.ruby-lang.org/pub/ruby/1.8/ruby-1.8.1.tar.gz
curl -Lo ruby-1.9.2-p0.tar.gz https://cache.ruby-lang.org/pub/ruby/1.9/ruby-1.9.2-p0.tar.gz
sha256sum -c <<'EOF'
d0f1e19fc7a14233ed0e1335867f7a7ddb18b85ab4c58795c5fc5a562710de4e  ruby-1.8.1.tar.gz
8c0c4e261a921b5c406bf9e76ac23bf3c915651534e9d1b9e8c5d0bee4a7285c  ruby-1.9.2-p0.tar.gz
EOF
tar xzf ruby-1.8.1.tar.gz
tar xzf ruby-1.9.2-p0.tar.gz
```

## 重要：源码树是纯净的

本目录下的 Ruby 源码**未经修改**。构建前所有 mingw32 / gcc12 兼容性补丁
都由 `tools/ruby_mingw_patches.py` 以幂等方式施加到**构建副本**
（`build/ruby-build/`），不污染原始源码树：

```bash
python tools/ruby_mingw_patches.py build/ruby-build
```

因此从上游 tarball 解出来的树可以直接参与构建，无需手工打补丁。
