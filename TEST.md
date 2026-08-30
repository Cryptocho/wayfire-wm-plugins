# TEST.md — 交互测试指南

## P1:ob-decor(openbox 主题装饰)— 待用户执行

自动化已全绿:obtheme 解析断言(OB-Arc-Dark + 渐变夹具 + 默认主题)、headless 冒烟。
以下是剩余的交互/视觉验证。

### 启动嵌套测试会话

```bash
cd ~/workspace/wm/wayfire-wm-plugins
WAYFIRE_PLUGIN_PATH=$PWD/build/ob-decor \
WAYFIRE_PLUGIN_XML_PATH=$PWD/metadata \
WLR_BACKENDS=wayland \
wayfire -c test/wayfire-decor.ini -d
```

会弹出 1280×720 嵌套窗口。`<super>T` 开 kitty(主桌面未占用的键)。

### D1 主题装饰外观(OB-Arc-Dark)

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | 开 kitty | 深色标题栏(#2F343F),标题居中浅灰蓝(#B5BCC9),外围 2px 紫色 ring |
| 2 | 观察右上按钮 | iconify(−)、maximize(□)、close(**红色圆**)——均为主题 xbm 图片 |
| 3 | 观察左上 | menu 按钮(↖);点击无动作(惰性,P2 接菜单) |
| 4 | 点击另一个窗口使 kitty 失焦 | 标题文字变暗(#7F8898),ring 变深色(#2a323b) |
| 5 | 重新聚焦 kitty | 标题变亮、ring 变紫(active 态) |

### D2 按钮动作

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | 双击标题栏 | 切换最大化(再双击还原) |
| 2 | 点击 maximize 按钮 | 同上;最大化后无边框(ring 也消失),还原后恢复 |
| 3 | 点击 iconify | 最小化(<super>TAB fast-switcher 可试;还原即可) |
| 4 | 点击 close | 窗口关闭 |
| 5 | hover 各按钮 | 图标变亮(hover 色 #B5BCC9 / close #D7787D) |

### D3 resize 边缘

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | 拖动窗口边缘(ring+边框区域) | 正常 resize,光标变对应箭头 |
| 2 | 拖动标题栏 | 移动窗口 |

### D4 主题切换与默认主题

```bash
# 嵌套会话运行中,编辑 test/wayfire-decor.ini: theme = (置空)
```
| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | 置空 theme 保存 | 无变化(热重载未接,重启生效) |
| 2 | 重启嵌套会话(theme 空) | labwc 内置默认装饰(细边框),不崩溃 |

### D5 已知未实现(勿报 bug)

- pressed 按下瞬间的专属颜色、maximize 的 toggled 态图标(主题里有,解析未接,T5+ 补)
- shade/desk/menu 按钮动作(P2+)
- 高分屏 scale>1 未验证

---

## P0:proto-pan(已全部通过,2026-08-30)

启动:`WAYFIRE_PLUGIN_PATH=$PWD/build/proto-pan WAYFIRE_PLUGIN_XML_PATH=$PWD/metadata WLR_BACKENDS=wayland wayfire -c test/wayfire.ini -d`
键盘 <super>F1/F2 步进、触摸板三指滑动自由平移、IPC `proto-pan/step|drag|get-state`。结论:全部通过,GO。
