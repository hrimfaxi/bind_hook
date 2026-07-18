# bind_hook.so - SO_REUSEPORT LD_PRELOAD hook

解决 dnsmasq、systemd-resolved 等 DNS 服务绑定 `0.0.0.0:53` 时不带 `SO_REUSEPORT`，导致 xray/xtp-rs 等 tproxy 程序无法复用端口的问题。

## 问题背景

```
dnsmasq:    bind(0.0.0.0:53)      -> 没有 SO_REUSEPORT, 独占端口
xray:       bind(8.8.8.8:53)      -> EADDRINUSE, 失败!
```

dnsmasq 等 DNS 服务默认绑定 `0.0.0.0:53` 且不设置 `SO_REUSEPORT`。当你想用 xray/xtp-rs 做 DNS 转发（tproxy 模式），它需要绑定具体 IP 的 53 端口，但由于端口已被占用且没有 REUSEPORT，bind 会失败。

## 解决方案

给 dnsmasq 等先占端口的服务注入 `LD_PRELOAD`，拦截其 `bind()` 调用。如果目标端口是 53（可配置），自动设置 `SO_REUSEPORT` + `SO_REUSEADDR`，然后调用真正的 bind。这样后续 xray 就能正常绑定同一端口了。

注入后：
```
dnsmasq:     bind(0.0.0.0:53)
                ↓ hook 自动注入
             setsockopt(SO_REUSEPORT)
             setsockopt(SO_REUSEADDR)
                ↓
             真正的 bind() -> 成功，端口可复用!

xray:        bind(8.8.8.8:53) -> 成功!
```

## 测试验证

```
$ ./test_bind 0.0.0.0:1053 2        # 无 hook
[0] bind(0.0.0.0:1053) OK
[1] bind(0.0.0.0:1053) FAILED: Address already in use

$ LD_PRELOAD=./bind_hook.so BIND_HOOK_PORT=1053 ./test_bind 0.0.0.0:1053 2  # 有 hook
[0] bind(0.0.0.0:1053) OK
[1] bind(0.0.0.0:1053) OK           # 复用成功!

$ ./test_bind 0.0.0.0:1053 1 &      # 模拟独占端口的服务 (无 hook)
$ LD_PRELOAD=./bind_hook.so BIND_HOOK_PORT=1053 ./test_bind 127.0.0.1:1053 1  # 模拟 xray (有 hook)
[0] bind(127.0.0.1:1053) OK         # 跨 IP 绑定成功!
```

## 编译

```bash
# 本地编译
make

# 交叉编译 (OpenWrt mipsel)
CC=mipsel-openwrt-linux-gnu-gcc make

# 或手动
mipsel-openwrt-linux-gnu-gcc -Wall -fPIC -shared -O2 -o bind_hook.so bind_hook.c -ldl
```

## 使用

### 基本用法

给 dnsmasq 等先占端口的服务加上 hook：

```bash
LD_PRELOAD=/path/to/bind_hook.so dnsmasq -k
```

### 自定义端口

默认拦截端口 53，可通过环境变量修改：

```bash
BIND_HOOK_PORT=5353 LD_PRELOAD=/path/to/bind_hook.so your_program
```

### 后台运行

```bash
LD_PRELOAD=/usr/lib/bind_hook.so /usr/sbin/dnsmasq
```

## OpenWrt 集成

### 方法1: 修改 dnsmasq init 脚本（推荐）

编辑 `/etc/init.d/dnsmasq`，在 `procd_open_instance` 后添加环境变量：

```bash
procd_open_instance $cfg
procd_set_param env LD_PRELOAD=/usr/lib/bind_hook.so
procd_set_param command $PROG -C $CONFIGFILE -k -x /var/run/dnsmasq/dnsmasq."${cfg}".pid
```

### 方法2: 全局环境变量

在 `/etc/environment` 中添加：

```
LD_PRELOAD=/usr/lib/bind_hook.so
```

**注意**: 这会影响所有程序，一般不建议使用。如果要用，确保 `BIND_HOOK_PORT` 设置合适的值，避免误拦截其他端口。

### 方法3: xray/xtp-rs 使用（不推荐）

也可以给 xray 自己用，但这样 dnsmasq 启动时仍然会独占端口，需要 xray 先启动或重启 dnsmasq：

```bash
LD_PRELOAD=/usr/lib/bind_hook.so /usr/bin/xray run -c /etc/xray/config.json
```

推荐给 dnsmasq 用，这样 xray 可以随时启动/重启。

## 调试

```bash
# 查看 hook 日志
LD_PRELOAD=/usr/lib/bind_hook.so xray run -c /etc/xray/config.json 2>&1 | grep bind_hook

# 使用 strace 验证 setsockopt 调用
LD_PRELOAD=/usr/lib/bind_hook.so strace -e setsockopt,bind your_program 2>&1 | grep -E "reuseport|53"

# 检查库是否加载
LD_PRELOAD=/usr/lib/bind_hook.so cat /proc/self/maps | grep bind_hook
```

## 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `BIND_HOOK_PORT` | `53` | 要拦截的目标端口 |

## 工作原理

1. 程序启动时，`LD_PRELOAD` 加载 `bind_hook.so`
2. 库中的 `bind()` 函数覆盖 libc 的 `bind()`
3. 每次调用 `bind()` 时，检查目标端口是否匹配
4. 如果匹配，自动调用 `setsockopt(SO_REUSEPORT)` 和 `setsockopt(SO_REUSEADDR)`
5. 然后调用真正的 `bind()`

## 适用场景

- OpenWrt + dnsmasq + xray/xtp-rs tproxy DNS 转发
- dnsmasq/systemd-resolved 等先占 53 端口的服务需要与其他程序共享端口
- 程序不方便修改源码但需要 REUSEPORT 的情况

## 注意事项

- `LD_PRELOAD` 对静态链接的程序无效
- 某些安全模块（SELinux 等）可能限制 `LD_PRELOAD`
- 确保 `bind_hook.so` 路径在文件系统上可访问（OpenWrt squashfs 只读分区需要提前放好）
- 端口 53 是特权端口，测试时需要 root 权限
