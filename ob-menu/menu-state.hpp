/* SPDX-License-Identifier: GPL-2.0-only */
/* menu-state — shared (process-wide) state for ob-menu: parsed menus + theme. */
#pragma once

#include "menu-model.hpp"
#include "menu-parse.hpp"

#include <obtheme.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace obmenu
{

/* Fonts/theme for one parsed configuration; shared by all outputs. */
struct MenuState
{
    MenuSet set;
    struct theme *ob = nullptr;
    bool initialized = false;

    void init(const std::string &menu_file_opt, const std::string &font,
        const std::string &font_menuitem, const std::string &font_menuheader)
    {
        if (initialized)
        {
            return;
        }

        /* The vendored theme engine keeps colors/layouts in ONE global rc
         * shim: only a single theme instance may exist per process. If
         * ob-decor (or any ob_* consumer) already initialized one, reuse
         * it — menu colors then come from the same themerc (e.g. the
         * active-item highlight color). */
        ob = ob_theme_current();
        if (!ob)
        {
            struct ob_theme_config cfg = {};
            cfg.theme_name      = "";
            cfg.font            = font.c_str();
            cfg.font_menuitem   = font_menuitem.c_str();
            cfg.font_menuheader = font_menuheader.c_str();
            ob_theme_init(&ob, cfg);
        }

        /* expand leading ~/ */
        std::string menu_file = menu_file_opt;
        const char *home = getenv("HOME");
        if ((menu_file.rfind("~/", 0) == 0) && home)
        {
            menu_file = std::string(home) + menu_file.substr(1);
        }

        bool have = !menu_file.empty() && parse_menu_file(menu_file, set);
        if (!have)
        {
            have = parse_menu_file("/etc/xdg/openbox/menu.xml", set);
        }

        add_default_menus(set);
        initialized = true;
    }

    static MenuState& get()
    {
        static MenuState state;
        return state;
    }
};

} // namespace obmenu
