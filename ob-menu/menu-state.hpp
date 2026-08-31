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
    bool initialized = false;
    /* config kept for lazy theme creation (see theme()) */
    std::string theme_name, font, font_menuitem, font_menuitem_active,
        font_menuheader;
    struct theme *theme_owned = nullptr;

    void init(const std::string &theme_name_opt,
        const std::string &menu_file_opt, const std::string &font,
        const std::string &font_menuitem,
        const std::string &font_menuitem_active,
        const std::string &font_menuheader)
    {
        if (initialized)
        {
            return;
        }

        this->theme_name      = theme_name_opt;
        this->font            = font;
        this->font_menuitem        = font_menuitem;
        this->font_menuitem_active = font_menuitem_active;
        this->font_menuheader      = font_menuheader;

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

    /* The vendored theme engine keeps colors/layouts in ONE global rc
     * shim: only a single theme instance may exist per process, and its
     * consumers initialize lazily (ob-menu at plugin init, ob-decor when
     * the first view gets decorated). Always read the live instance so
     * the menu follows the most recently initialized theme (typically
     * ob-decor's themerc, e.g. the menu highlight color); if nobody has
     * initialized one yet, build our own from the ob-menu options. */
    struct theme *theme()
    {
        if (auto *t = ob_theme_current())
        {
            return t;
        }

        if (!theme_owned)
        {
            struct ob_theme_config cfg = {};
            cfg.theme_name      = theme_name.c_str();
            cfg.font            = font.c_str();
            cfg.font_menuitem   = font_menuitem.c_str();
            cfg.font_menuheader = font_menuheader.c_str();
            ob_theme_init(&theme_owned, cfg);
        }

        return theme_owned;
    }

    /* per-state item font: selected entries use font_menuitem_active
     * when configured (labwc <font place="MenuItem"> spirit) */
    const std::string& menuitem_font(bool selected) const
    {
        return (selected && !font_menuitem_active.empty()) ?
            font_menuitem_active : font_menuitem;
    }

    static MenuState& get()
    {
        static MenuState state;
        return state;
    }
};

} // namespace obmenu
