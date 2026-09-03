# TEST.md — 交互测试指南

*最新的排在最上面;越往下越旧(已通过的阶段沉底)。*

## P6:ob-theme 主题 provider + ob-switcher(主题化窗口切换 OSD)(已全部通过,2026-09-04)

背景:ob-theme 插件统一持有 openbox 主题(ob-decor/ob-menu/ob-switcher 都跟随它,provider 缺席时各插件仍能用旧 fallback);ob-switcher = fast-switcher 复制 + labwc 式 OSD,主题色取 themerc 的 `osd.*` / `window_switcher.*` 键。自动化已全绿(ob-theme provider 状态/reload、switcher 正反向/Esc 取消/OSD 生命周期,共 17 断言),本节验证**视觉与真实交互**。

### 启动嵌套测试会话

```bash
cd ~/workspace/wm/wayfire-wm-plugins
WAYFIRE_PLUGIN_PATH=$PWD/build/ob-theme:$PWD/build/ob-switcher:$PWD/build/ob-decor:$PWD/build/ob-menu:$PWD/build/layout-manager \
WAYFIRE_PLUGIN_XML_PATH=$PWD/metadata \
WLR_BACKENDS=wayland wayfire -c test/wayfire-switcher.ini -d
```
(test/wayfire-switcher.ini 的 plugins 行含全部五插件:ob-theme + ob-switcher + ob-decor + ob-menu + layout-manager(S-4 需要),另有 move/resize 供标题栏拖动;用 `<super>T` 多开几个窗口;主题 OB-Arc-Dark。想换主题:ini `[ob-theme] theme_name` 改成 `~/.themes` 下任意主题,重启会话即可)

### S-1 OSD 基本外观(主题化)

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | `<alt>+Tab` 按住 | 屏幕**中央**出现 OSD 面板:主题 `osd.bg.color` 底、`osd.border.color` 边框;每行 = 图标占位块(应用首字母)+ app_id + 标题 |
| 2 | 选中行观察 | 高亮 = 主题 `window_switcher.item.active.bg.color` 底 + `item.active.border.color` 边框,宽度铺满行 |
| 3 | 松开 `<alt>` | OSD 消失,焦点落到选中行窗口,窗口激活态(装饰 active 色) |

### S-2 切换行为(labwc 语义)

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | 按住 `<alt>` 连按 Tab | 高亮行每次移到下一窗口,非选中窗口变半透明(`inactive_alpha`),装饰切 inactive |
| 2 | 加 Shift 再按 Tab | 高亮反向移动 |
| 3 | 按住期间按 `Esc` | OSD 消失、**焦点不变**,所有窗口恢复原透明度/激活态 |
| 4 | OSD 可见时鼠标点击另一行 | 焦点立即切到该行窗口,OSD 关闭 |
| 5 | 只有一个窗口时 `<alt>+Tab` | 无 OSD(候选集为空不做任何事) |

### S-3 主题跟随 provider

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | ob-theme 在场(上面会话) | ob-decor 标题栏、ob-menu 菜单、ob-switcher OSD 三者颜色一致(同一主题实例) |
| 2 | 注释 plugins 行里的 `ob-theme` 重启会话 | 三者仍正常(fallback 路径):ob-decor/ob-menu 各自懒初始化,OSD 主题色仍生效 |
| 3 | ob-theme 不在场且窗口未出现前开菜单? | 跳过——直接验证:无 ob-theme 会话中 OSD 颜色与 ob-decor 装饰色一致即可 |

### S-4 与其他插件共处

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | tiling 模式(`<super>+Shift+T` 收编)后 `<alt>+Tab` | OSD 列出平铺窗口,切换正常 |
| 2 | 切换器开着时开右键菜单(ob-decor 标题栏右键) | 切换器与菜单不叠加输入混乱(顺序行为,允许一个先关) |
| 3 | 切换期间关闭一个候选窗口 | 候选列表收缩,不崩溃,选中项回退正常(自动化已覆盖,视觉复核) |

已知差异(与 labwc 对齐项,见计划):无窗口实时预览(preview)与轮廓(outlines)——它们是 labwc cycle 的 reparent 手法,列为后续;OSD 超 12 行截断(无滚动条);图标为占位块(libsfdo 图标为后续)。

---

## P4.5:tiling 几何动画(平滑滑动)— 待用户执行

### 启动嵌套测试会话

```bash
cd ~/workspace/wm/wayfire-wm-plugins
WAYFIRE_PLUGIN_PATH=$PWD/build/layout-manager:$PWD/build/ob-decor:$PWD/build/ob-menu \
WAYFIRE_PLUGIN_XML_PATH=$PWD/metadata \
WLR_BACKENDS=wayland wayfire -c test/wayfire-layout-adaptive.ini -d
```
(默认动画 300ms;`animation_duration` 选项可调,0 = 关闭,改 ini 即时生效无需重启)

### A-1 日常操作动画

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | `<super>T` 开新窗口 | 新窗与被分割的旧窗**平滑滑到**新位置(装饰一起飞),无瞬间跳变 |
| 2 | 关闭一个窗口 | 邻居窗口平滑扩张补位 |
| 3 | `<super>+Shift+Space` 浮动/收回 | 平滑浮出/滑回树内 |
| 4 | 拖拽分割线/窗口边框 | **无动画**——几何实时跟手(松手即定位) |

### A-2 模式切换与恢复

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | 浮动布局按 `<super>+Shift+T` 收编 | 全部窗口从各自位置**同时平滑飞到**平铺位 |
| 2 | 再按切回 stacking | 窗口平滑滑回收编前的原位置 |
| 3 | 动画中再切换/再开关窗 | 从当前位置继续插值,不闪跳 |

### A-3 禁用与时长

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | ini 改 `animation_duration = 0` 保存 | 立即回到瞬时切换(旧行为) |
| 2 | 改回 `300` 或试 `150`/`500` | 时长即时生效 |

---

## P4:adaptive 平铺样式 + 跟随鼠标焦点 — 待用户执行

(tabbed/stacked 按用户决定取消;本节为替代的 P4 内容)

### 启动嵌套测试会话

```bash
cd ~/workspace/wm/wayfire-wm-plugins
WAYFIRE_PLUGIN_PATH=$PWD/build/layout-manager:$PWD/build/ob-decor:$PWD/build/ob-menu \
WAYFIRE_PLUGIN_XML_PATH=$PWD/metadata \
WLR_BACKENDS=wayland wayfire -c test/wayfire-layout-adaptive.ini -d
```
(配置已含 `tiling_layout = adaptive`、`focus_follows_mouse = true`、vswitch `<super>F1/F2`、root menu `<super>F3`;**`<super>T` 开新 kitty**——测螺旋就是反复按它)

### T-1 adaptive 减半插入(螺旋)

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | 开窗口 1 | 占满工作区 |
| 2 | 开窗口 2(焦点在窗口 1) | 宽>高 → 左右分:旧窗留左,新窗在右,**焦点给新窗** |
| 3 | 开窗口 3(焦点在窗口 2) | 窗口 2 现在窄了,高>宽 → 上下分:窗口 2 留上,窗口 3 在下 |
| 4 | 继续开窗口 4、5… | 不断分割焦点窗的最大边,越分越接近正方形(收缩螺旋) |
| 5 | 关闭中间任一窗口 | 空间被其"对半伙伴"收回,其余不动 |

### T-2 adaptive 与既有功能交互

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | adaptive 下切到 stacking 再切回 | 收编按行主序重建螺旋;切回浮动恢复原几何(与 tiling 一致) |
| 2 | `<super>+Shift+Space` 浮动某窗 | 与 tiling 相同:浮出/收回,几何记忆 |
| 3 | 边框/缝隙拖拽 | 照常工作(被分割的伙伴联动) |
| 4 | client-menu → Fullscreen | 照常 |

### T-3 focus_follows_mouse(此 ini 已开启)

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | 鼠标在平铺窗口间移动 | 焦点跟到光标所在窗口(标题栏高亮 active 态切换) |
| 2 | 鼠标移到浮动窗上 | **不**跟随(浮动窗仍点击聚焦) |
| 3 | 切到 stacking 模式 | 跟随完全关闭(原生行为) |
| 4 | ini 中 `focus_follows_mouse = false` 重启 | 跟随关闭,回到点击聚焦 |

### 已知未实现(勿报 bug)

- adaptive 收编(从 stacking 切入)按行主序逐个走减半链,布局可能与手动逐个开窗不同(算法决定的,正常)
- `tiling_layout` 运行时改选项即时生效,但已建好的树不重排(下次插入生效);想立刻应用请切换一次模式

---

## P3:layout-manager(全局布局模式:sway H/V tiling ↔ stacking)— 待用户执行

### 启动嵌套测试会话

```bash
cd ~/workspace/wm/wayfire-wm-plugins
WAYFIRE_PLUGIN_PATH=$PWD/build/layout-manager:$PWD/build/ob-decor:$PWD/build/ob-menu \
WAYFIRE_PLUGIN_XML_PATH=$PWD/metadata \
WLR_BACKENDS=wayland wayfire -c test/wayfire-layout-interactive.ini -d
```
(嵌套窗口里开几个 kitty:`<super>T` 或终端输入 kitty;`<super>1..4` 切工作区)

### L-1 模式切换(收编/恢复)

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | 浮动摆放 3-4 个窗口(大小位置各异) | 原生浮动,可自由拖动 |
| 2 | `<super>+Shift+T` | 所有窗口瞬间收编:等宽并排(外缘 8px、窗口间 8px 缝隙),行主序(上→下、左→右) |
| 3 | 再按 `<super>+Shift+T` | 每个窗口**恢复到收编前的位置和大小** |
| 4 | 反复切换 3-4 次 | 无闪烁残留、无 crash、浮动窗(见 L-4)位置不动 |

### L-2 tiling 下的窗口生命周期

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | tiling 模式,开新窗口 | 插到**当前焦点窗口右侧**(同层兄弟),焦点给新窗口 |
| 2 | 关闭中间一个窗口 | 左右两侧平分腾出的空间,其余不动 |
| 3 | 最小化一个窗口 | 立即让出空间;还原后重新进树(插在最后焦点处) |
| 4 | `<super>F1` / `<super>F2`(嵌套会话已配 vswitch;root menu 键在嵌套配置中让位为 `<super>F3`) | ws1/ws2 各有独立树;互不影响 |
| 5 | 移动窗口到另一工作区(client-menu 或 move) | 目标工作区树里正确排列 |

### L-3 resize:拖窗口边框(主)+ 分割线缝隙(两种都支持)

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | 按住某窗口**右侧边框**向右拖 | 该窗口变宽,**右侧邻居相应变窄**(sway 语义:窗口缩放联动邻接窗口) |
| 2 | 按住某窗口**左边框**向左拖 | 该窗口变宽,左侧邻居变窄 |
| 3 | 上下边框同理(竖排 split 时) | 高度联动 |
| 4 | 两窗口之间的缝隙(约 8px 带,±4px 容差)按下拖动 | 同样调整两侧(与边框拖拽等效) |
| 5 | 拖到极限(≤100px 宽/≤60px 高) | 钳住不缩,不崩 |
| 6 | 在窗口**内部**(非边框区)按住左键拖动 | 不改变布局 |
| 7 | 最左窗口的左边框 / 最右窗口的右边框 | 无法再拖(外侧无边;原生行为即无响应) |

### L-4 per-view float(super+Shift+Space)

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | tiling 模式,焦点在某窗口,`<super>+Shift+Space` | 该窗口浮出:约 50%×75% 居中,可自由移动/缩放(原生行为),浮在平铺窗口之上 |
| 2 | 再按 `<super>+Shift+Space` | 回到树里(插在最后焦点位置),尺寸与相邻窗口协调 |
| 3 | 再浮出 | **恢复上一次浮动的位置和大小**(几何记忆) |
| 4 | 浮动状态下切换模式/切换工作区 | 浮动窗位置不动、状态保持 |
| 5 | 浮动窗移到 ws2 | 在 ws2 仍是浮动(z 序在平铺窗之上) |

### L-5 split h/v(手动分割)

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | 焦点在某平铺窗口,`<super>V` | 无可见变化(包了一层竖排 split) |
| 2 | 开新窗口 | 新窗口出现在焦点窗口**下方**(竖排堆叠),而不是右侧 |
| 3 | `<super>B` 后再开窗口 | 回到左右并排方向 |
| 4 | 新窗口开在别的焦点上 | 不受之前 split 影响(仅对执行 split 时的焦点窗口生效) |

### L-6 右键与全屏

| 步骤 | 操作 | 预期 |
|---|---|---|
| 1 | tiling 模式,右键**外圈缝隙**(窗口与屏幕边缘之间的桌面) | root menu 弹出 |
| 2 | 右键窗口标题栏 | client menu(原有行为) |
| 3 | client-menu → Fullscreen / 或窗口自身请求全屏 | 占满整屏(含缝隙);新窗口进来时全屏退出 |
| 4 | 全屏下拖拽分割线 | 不响应(全屏期间禁用) |

### 已知未实现(勿报 bug)

- 键盘焦点移动绑定(key_focus_*)默认未设,ini 配置后可用
- set-layout(任意树重构 IPC)、tabbed/stacked 布局 → P4
- scrolling 自由平移 → P5
- 拖拽窗口重排(交换位置)→ P4;scale>1 未验证

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

## P0:proto-pan(已全部通过,2026-08-30)

启动:`WAYFIRE_PLUGIN_PATH=$PWD/build/proto-pan WAYFIRE_PLUGIN_XML_PATH=$PWD/metadata WLR_BACKENDS=wayland wayfire -c test/wayfire.ini -d`
键盘 <super>F1/F2 步进、触摸板三指滑动自由平移、IPC `proto-pan/step|drag|get-state`。结论:全部通过,GO。
