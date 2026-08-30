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

### D3 resize/move 拖动(前置:会话需在配置含 move/resize 后启动)

> 配置已含 `move resize`(装饰手势的执行者:decor 节点检测手势→发请求信号→
> move/resize 插件执行)。若你改过配置,请**重启嵌套会话**再测。

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

## P2:ob-menu(openbox 菜单系统)— 待用户执行

自动化已全绿:M1 解析断言(obmenu-test)、M5 动作/动态菜单 14 断言(m5-test)、
M6 pipemenu 19 断言(pipe-menu.sh:懒 spawn/重开重生成/'<' 校验/4s 超时/1MiB 上限)。
以下是剩余的交互/视觉验证。

### 启动嵌套测试会话

```bash
cd ~/workspace/wm/wayfire-wm-plugins
WAYFIRE_PLUGIN_PATH=$PWD/build/ob-decor:$PWD/build/ob-menu \
WAYFIRE_PLUGIN_XML_PATH=$PWD/metadata \
WLR_BACKENDS=wayland \
wayfire -c test/wayfire-menu-interactive.ini -d
```

会弹出 1280×720 嵌套窗口(OB-Arc-Dark 装饰)。`<super>T` 开 kitty。
键位:`<super>F1` = root-menu,`<alt>Space` = client-menu,菜单内 ↑↓←→/Enter/Esc。

### M-1 菜单外观与键盘导航

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | `<super>F1` | 光标处弹出 root-menu:深色底、边框色描边、分隔行 "Session" 带标题 |
| 2 | ↑/↓ 移动 | 高亮条跳过分隔线;"Session" 行不可选中 |
| 3 | 在 Windows/Apps/Pipe 上按 → | 子菜单贴着父项展开;← 收回一级;Esc 逐级关闭 |
| 4 | Esc 至根层再按 | 整树关闭 |

### M-2 鼠标交互

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | `<super>F1` 后移动鼠标 | hover 高亮跟随;子菜单自动展开/收回 |
| 2 | Windows → 点窗口列表中 kitty | 该视图被激活(菜单先关、焦点后给) |
| 3 | 点击菜单外任意处 | 整树关闭 |
| 4 | 按住 `<super>F1` 打开立即松开 | 松开那次不误触选择(release-guard) |

### M-3 客户菜单入口

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | kitty 聚焦按 `<alt>Space` | client-menu(Minimize/Maximize/Close)锚在视图标题栏附近 |
| 2 | 装饰标题栏**右键** | client-menu 锚在光标处 |
| 3 | 点标题栏左上 menu 按钮(↖) | client-menu 弹出 |
| 4 | 对支持 xdg show_window_menu 的 GTK 应用(如 nautilus)在客户区要菜单 | client-menu 锚在应用请求位置(clamp 在 workarea 内) |
| 5 | 依次试 Minimize / Maximize / Close | 动作生效;菜单先关、焦点回窗口 |

### M-4 动作白名单(部分已由 M5 自动化覆盖,人工复核)

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | `<super>F1` → Terminal | 新 kitty 实例,获得焦点 |
| 2 | Apps → More → Editor | 三级子菜单正常;zedit 不存在则仅日志报 spawn 失败,不崩 |
| 3 | client-menu → Maximize | 切换平铺/还原(即最大化) |
| 4 | client-menu → Fullscreen?(若有) | 无此菜单项则跳过(client-menu 夹具只含 3 项) |

### M-5 pipemenus(夹具:Pipe/Pipe_slow/Pipe_huge)

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | `printf '<menu id="pipe-demo" label="Pipe">\n  <item label="Hello"><action name="Execute" command="true" /></item>\n</menu>\n' > /tmp/obmenu-pipe.xml` | 准备脚本输出 |
| 2 | `<super>F1` → hover Pipe | 短暂等待后子菜单弹出 Hello(异步 spawn `cat`) |
| 3 | 关树重开 hover Pipe | 重新 spawn:改写 /tmp/obmenu-pipe.xml 后内容随之更新 |
| 4 | hover Pipe_slow | 高亮停住不展开(等待中其他选择被阻塞);~4s 后放弃,菜单不崩 |
| 5 | hover Pipe_huge | 2MB 输出触发 1MiB 上限,无子菜单,wayfire 正常 |
| 6 | `rm /tmp/obmenu-pipe.xml` 后 hover Pipe | 输出为空:无子菜单,不崩(日志 `does not start with '<'`) |

日志查看:嵌套会话 stderr 即 wayfire 日志(或 `-d` 输出)。

### 已知未实现(勿报 bug)

- 菜单图标渲染、use_markup 富文本(数据模型已留字段)
- ToggleAlwaysOnTop/Omnipresent/Shade/Decorations:渲染但 noop(wayfire 无公开 API)
- If/ForEach/Move/Resize 等复杂动作:解析时警告跳过
- scale>1 渲染未验证

---

## P0:proto-pan(已全部通过,2026-08-30)

启动:`WAYFIRE_PLUGIN_PATH=$PWD/build/proto-pan WAYFIRE_PLUGIN_XML_PATH=$PWD/metadata WLR_BACKENDS=wayland wayfire -c test/wayfire.ini -d`
键盘 <super>F1/F2 步进、触摸板三指滑动自由平移、IPC `proto-pan/step|drag|get-state`。结论:全部通过,GO。
