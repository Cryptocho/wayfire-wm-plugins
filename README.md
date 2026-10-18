# wayfire-wm-plugins

Openbox/labwc-flavored window management for [Wayfire](https://github.com/WayfireWM/wayfire),
implemented entirely as **out-of-tree plugins** (no wayfire core changes):

- **openbox themerc theming** for window decorations, menus and the window
  switcher OSD;
- **openbox menu.xml** menus (submenus, pipemenu, dynamic client lists);
- a **theme provider**, window switcher and `ext_workspace_v1` server plugin.

## Plugins

| Plugin | What it does |
|---|---|
| `libobtheme` | Vendored labwc theme engine (labwc @ `5aaa5da0571b`, ~minimal diff) exposing an `obtheme.h` C API: themerc parsing, button image rendering, font handling. Built as a **shared library** so exactly one theme instance exists per process. |
| `ob-decor` | Server-side decorations themed by openbox themerc: titlebar (fill/gradient/justify), border strips, outer ring (active+inactive colors), xbm buttons with hover. |
| `ob-menu` | openbox menu.xml menus: submenu/pipemenu/dynamic client-list menus, keyboard + mouse navigation, opened via keybinding, titlebar right-click, menu button or `xdg show_window_menu`. |
| `ob-theme` | Theme **provider**: owns the single `libobtheme` theme instance, publishes it to other plugins via core data + reload signal, offers in-place hot-reload (IPC `ob-theme/debug`). |
| `ob-switcher` | Window switcher (fast-switcher behavior + labwc-cycle-style OSD): `<alt>+Tab` (labwc default), Esc cancels, click an OSD row to select; OSD colors/sizes come from themerc `osd.*` / `window_switcher.*` keys. |
| `ext-workspace` | Implements the `ext_workspace_v1` Wayland protocol for workspace groups, workspace state and activation. |

## Building

Requires Wayfire 0.11.x development files (pkg-config `wayfire`), cairo,
pangocairo, libpng, glib-2.0, libxml-2.0, libdrm and wayland-server.

```sh
meson setup build
meson compile -C build
```

## Running

Point wayfire at the build outputs and add the plugins to your ini:

```sh
WAYFIRE_PLUGIN_PATH="$PWD/build/ob-theme:$PWD/build/ob-decor:$PWD/build/ob-menu:$PWD/build/ob-switcher:$PWD/build/ext-workspace" \
WAYFIRE_PLUGIN_XML_PATH="$PWD/metadata" \
wayfire -c your-test.ini
```

```ini
[core]
plugins = ob-theme ob-decor ob-menu ob-switcher ext-workspace ...
preferred_decoration_mode = server

[ob-theme]      ; optional: theme/fonts for all ob_* consumers
theme_name = OB-Arc-Dark
```

`ob-theme` should be listed first (it initializes the theme before the other
plugins), but everything works without it too — consumers fall back to lazy
initialization. Default bindings: `<super>F1` root menu, `<alt>+Space`
client menu, `<alt>+Tab` window switcher — see each `metadata/*.xml`.

## Testing

Automated (headless) suites live in `scripts/`, e.g.

```sh
bash scripts/obtheme-test.sh     # themerc parsing fidelity
bash scripts/obmenu-test.sh      # menu.xml parsing
bash scripts/decor-destroy-test.sh
bash scripts/ob-theme-test.sh
bash scripts/m5-test.sh          # menu actions over IPC
bash scripts/ob-switcher-test.sh # vinput-driven switcher assertions
```

`TEST.md` documents the interactive (manual) checks per development phase;
`AGENTS.md` records design decisions, phase conclusions and hard-won lessons;
`libobtheme/VENDORED-DIFFS.md` lists every local change vs upstream labwc.

## License

GPL-2.0-only, same as labwc/wayfire. Vendored labwc sources live under
`libobtheme/` with provenance banners.
