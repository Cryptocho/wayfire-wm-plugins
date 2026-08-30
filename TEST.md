# TEST.md — proto-pan 交互测试指南(P0)

自动化部分(构建、加载、热重载、平移状态机 18 项断言、动画收敛)**已由 agent 全部验证通过**。
本文件是剩余的**交互/视觉验证**,需要你在嵌套会话里手动执行。

> 安全说明:嵌套实例独立于你的主桌面(wayland-1),退出只需关闭那个窗口或在终端 Ctrl+C。

## 启动嵌套测试会话

```bash
cd ~/workspace/wm/wayfire-wm-plugins
WAYFIRE_PLUGIN_PATH=$PWD/build/proto-pan \
WAYFIRE_PLUGIN_XML_PATH=$PWD/metadata \
WLR_BACKENDS=wayland \
_WAYFIRE_SOCKET=$XDG_RUNTIME_DIR/wayfire-wm-plugins.sock \
wayfire -c test/wayfire.ini -d
```

会弹出一个 **1280×720** 的嵌套 wayfire 窗口,画面应为**整屏紫色**(轨道 tile 0)。

手动平移命令(另开一个终端;`dir` 为 1/-1):

```bash
python3 - $XDG_RUNTIME_DIR/wayfire-wm-plugins.sock <<'EOF'
import json, socket, struct, sys
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1])
p = json.dumps({"method": "proto-pan/step", "data": {"dir": int(sys.argv[2]) if len(sys.argv) > 2 else 1}}).encode()
s.sendall(struct.pack("<I", len(p)) + p)
h = b""
while len(h) < 4: h += s.recv(4 - len(h))
(l,) = struct.unpack("<I", h); b = b""
while len(b) < l: b += s.recv(l - len(b))
print(b.decode())
EOF
```

---

## T1 视觉:三色轨道 + 无残影(S2/S5)

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | 启动嵌套会话 | 整屏紫色(#4a1060 附近) |
| 2 | `<super>F2` | **平滑动画**(约 300ms)过渡到整屏绿色,无残影、无撕裂 |
| 3 | `<super>F2` ×2 | 第二次到橙色后停住(轨道末端 clamp);动画中连按应流畅接续 |
| 4 | `<super>F1` ×3 | 依次绿→紫,最后停住(起始端 clamp) |
| 5 | 动画中随意来回按 F1/F2 | 从当前位置平滑改向,不跳变 |

## T2 键盘平移 + 真实窗口输入命中(S3,核心)

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | 嵌套会话内按 `<super>T`(`<super>Enter` 已被主桌面占用) | kitty 启动,**但画面仍是紫色**——kitty 被收编到了 tile 1(日志 `adopted view 'kitty'`) |
| 2 | `<super>F2` | 绿色出现,kitty 叠在其上(它在 x=1280 等着你) |
| 3 | **点击 kitty 并输入命令**(如 `echo hi`) | 能聚焦、能打字 → **输入命中跟随平移 ✓(本原型最关键验证点)** |
| 4 | `<super>F1` 回 tile 0,点击 kitty 原本的位置 | 不应命中任何东西(视口外),光标行为如常 |

## T3 触摸板三指滑动连续平移(S4,核心)

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | 触摸板**三指水平滑动** | 内容**跟手**连续移动(自然方向:手指向左,内容向左) |
| 2 | 滑过半屏后松手 | 吸附到最近一屏(带 300ms 动画) |
| 3 | 在 tile 0 位置继续向右滑 | 停在起始端,不越界 |
| 4 | 滑到末端(橙色)继续向左滑 | 停在轨道末端 |
| 5 | 滑动进行中观察光标 | 抓取期间指针事件被平移消费,松手后恢复悬停/点击 |

若手指数不触发,检查触摸板驱动;可把 `test/wayfire.ini` 里 `[proto-pan] swipe_fingers`(默认 3)改成 2 试。

## T4 动画手感调节(S5)

在 `test/wayfire.ini` 追加后重启嵌套会话(或用热重载):

```ini
[proto-pan]
duration = 100    # 快;再试 1000 慢速
```

按 F2 对比手感。顺手试试连续快按 F2——应连续平滑改向。

---

## 结果反馈

全部跑完后告诉我:哪些通过、哪些不符合预期(截图/录屏更好)。
我据此更新 AGENTS.md 的原型结论(go/no-go 与已知限制清单)。
