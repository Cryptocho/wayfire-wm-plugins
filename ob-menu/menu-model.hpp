/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * menu-model — data model for ob-menu's parsed menu tree.
 * Field semantics mirror labwc's menu.c data structures (vendor reference:
 * labwc@5aaa5da0571b include/menu/menu.h), but this is new C++ code, not a
 * vendored file.
 */
#pragma once

#include <map>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>

namespace obmenu
{

enum class ItemType
{
    Item,       /* selectable, runs actions */
    Separator,  /* horizontal line, optional label = title row */
    Title,      /* header row */
};

struct Action
{
    std::string name;                         /* e.g. "Execute" */
    std::map<std::string, std::string> attrs; /* e.g. command → kitty */
};

struct MenuItem;

/* One menu (a vertical list of items), identified by a unique id. */
struct Menu
{
    std::string id;
    std::string label;   /* for titles when referenced as a submenu */
    std::string execute; /* non-empty = pipemenu definition */
    int width_min  = 0;
    int width_max  = 0;  /* 0 = no clamp */
    bool is_pipemenu_cached = false; /* children were spawned at least once */

    std::vector<std::unique_ptr<MenuItem>> items;
};

struct MenuItem
{
    ItemType type = ItemType::Item;
    std::string text;
    std::string icon; /* reserved (rendering deferred) */
    bool selectable   = true;
    std::vector<Action> actions;
    Menu *submenu     = nullptr; /* raw pointer into the registry */
    int accelerator   = -1;      /* index of '_' in text, -1 = none */
    uint32_t id       = 0;       /* stable id for IPC assertions */
};

/* The whole parsed menu system (registry + defaults). */
struct MenuSet
{
    std::map<std::string, std::unique_ptr<Menu>> menus;
    uint32_t next_id = 1;

    Menu *find(const std::string &id) const;
    Menu *create(const std::string &id, const std::string &label);
    uint32_t next_item_id()
    {
        return next_id++;
    }
};

/* labwc's predefined dynamic menus: empty definitions whose items are
 * rebuilt from the live view list each time a menu tree opens. */
static constexpr const char *DYN_MENU_CLIENT_LIST  = "client-list-combined-menu";
static constexpr const char *DYN_MENU_CLIENT_SEND_TO = "client-send-to-menu";

inline bool is_dynamic_menu(const std::string &id)
{
    return (id == DYN_MENU_CLIENT_LIST) || (id == DYN_MENU_CLIENT_SEND_TO);
}

/* Build the built-in default menus (labwc init_rootmenu/init_windowmenu
 * equivalents) when no menu.xml provides them. */
void add_default_menus(MenuSet &set);

} // namespace obmenu
