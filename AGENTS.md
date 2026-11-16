# AGENTS.md — `/home/cryptocho/workspace/wm` 工作约定

## 这是什么项目

在 Wayfire 上以 **out-of-tree 插件**(不修改 wayfire core)实现两件事:

1. **labwc 式窗口装饰和菜单**——兼容 openbox 主题(themerc)与 openbox 菜单(menu.xml)
2. **运行时动态切换的全局布局系统**——sway 式 tiling、scroll 式 scrolling(自由平移)、labwc 式 stacking(浮动);全局同一时刻一个模式,可运行时切换;tiling/scrolling 内单个窗口可像 sway 那样提出为浮动窗口(per-view float)

## 目录约定

- 参考仓库 **READ-ONLY,一律不得修改**:`wayfire/`、`labwc/`、`sway/`、`scroll/`
- `wayfire_plugin_arch.md`:wayfire 插件架构笔记(上游文档)
- 插件代码交付在新仓库(如 `wayfire-wm-plugins/`);需要复用参考仓库代码时**复制过来再改造**

## 工作规则

- 全程中文交流
- 缺少依赖/程序:直接报告名称,由用户安装;**禁止 sudo**
- git push 通过 `$GIT_PAT` 环境变量认证;不得把 token 写进命令行或任何文件

## Xvfb 无头集成测试

- 要在无用户桌面干扰的情况下验证 Wayfire 插件，可用 Xvfb 提供独立 X11 显示，再以 `WLR_BACKENDS=x11` 启动嵌套 Wayfire；这是真实 Wayland compositor，不等同于 `WLR_BACKENDS=headless` 的仅加载检查。
- 使用独立、权限为 `0700` 的 `XDG_RUNTIME_DIR`，并用 `Xvfb -displayfd 3 -screen 0 1280x720x24 -nolisten tcp 3>"$runtime/display" &` 动态分配显示号；从 `$runtime/display` 读取号码后设置 `DISPLAY=:$display`。Wayfire 日志中的 `Using socket name wayland-N` 是客户端所用的 `WAYLAND_DISPLAY`。
- 设置 `WLR_BACKENDS=x11`、`WLR_RENDERER=pixman`、`WAYFIRE_PLUGIN_PATH`（冒号分隔各插件的 `build/<plugin>` 目录）、`WAYFIRE_PLUGIN_XML_PATH=<仓库>/metadata`、`XDG_RUNTIME_DIR` 和独立的 `_WAYFIRE_SOCKET`，然后用目标 ini 启动 `wayfire -c <ini> -d` 并保存日志。
- 测试 ini 的插件必须写在 `[core] plugins =`；按测试覆盖范围列全所需插件。需验证窗口/装饰/切换器时，通过 Wayland socket 启动 Kitty；需验证 ext-workspace 时再配置对应插件与客户端。IPC 使用 `_WAYFIRE_SOCKET` 指向的 socket。
- 冒烟断言至少确认日志里的插件加载/初始化、Wayfire 仍存活及目标 IPC 行为。例如 `ob-theme/debug` 检查主题状态，`ob-menu/debug` 执行 `open-root` 后检查 `state.open` 并关闭，`ob-switcher/debug` 先 `activate` 再检查 `nr_views`/`osd_visible`，`ext-workspace/get-state` 检查工作区组。切换器未激活时 `nr_views=0` 不代表没有映射窗口；激活时才更新候选视图列表。
- 可在该会话中用 `grim` 截取实际 Wayfire 输出；视觉效果由用户肉眼确认，不新增像素采样断言。
- 启动 Xvfb、Wayfire、测试客户端时立即记录各自 `$!`，清理只针对这些精确 PID；禁止用进程名或命令行搜索 PID。测试结束移除临时运行目录和配置，不触碰用户会话。
