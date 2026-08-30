/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * menu-parse — openbox menu.xml parser for ob-menu.
 * Mirrors labwc fill_menu/fill_item semantics (roles a/b/c, separators,
 * accelerators, dotted-attribute actions) using libxml2 + the vendored
 * lab_xml_* helpers. New code, not vendored.
 */
#include "menu-parse.hpp"
#include "menu-model.hpp"

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <cstring>
#include <cstdio>
#include <fstream>

#include "common/xml.h"

namespace obmenu
{
namespace
{
MenuSet *g_set = nullptr;

std::string node_content(xmlNode *node)
{
    xmlChar *content = xmlNodeGetContent(node);
    std::string result = content ? (char *)content : "";
    xmlFree(content);
    /* strip leading/trailing whitespace */
    size_t b = result.find_first_not_of(" \t\n\r");
    if (b == std::string::npos)
    {
        return "";
    }

    size_t e = result.find_last_not_of(" \t\n\r");
    return result.substr(b, e - b + 1);
}

std::string node_attr(xmlNode *node, const char *name)
{
    xmlChar *value = xmlGetProp(node, (const xmlChar *)name);
    std::string result = value ? (char *)value : "";
    xmlFree(value);
    return result;
}

/* Parse the <action> children of an <item> (or nested <action> lists).
 * Dotted attributes (<command a.b="c"/>) are expanded by
 * lab_xml_expand_dotted_attributes into child nodes. */
void parse_actions(xmlNode *item_node, std::vector<Action> &out)
{
    for (xmlNode *child = item_node->children; child; child = child->next)
    {
        if (child->type != XML_ELEMENT_NODE)
        {
            continue;
        }

        if (xmlStrcmp(child->name, (const xmlChar *)"action") == 0)
        {
            lab_xml_expand_dotted_attributes(child);
            Action action;
            /* attributes (incl. "name") have been converted to children */
            for (xmlNode *attr = child->children; attr; attr = attr->next)
            {
                if (attr->type != XML_ELEMENT_NODE)
                {
                    continue;
                }

                std::string key = (const char *)attr->name;
                std::string value = node_content(attr);
                if (key == "name")
                {
                    action.name = value;
                }
                else
                {
                    action.attrs[key] = value;
                }
            }

            out.push_back(action);
        } else if (xmlStrcmp(child->name, (const xmlChar *)"menu") != 0)
        {
            /* unknown child — libwc ignores them for items */
        }
    }
}

MenuItem *add_arrow_item(Menu &parent_menu, Menu &submenu)
{
    auto item  = std::make_unique<MenuItem>();
    item->text = submenu.label.empty() ? submenu.id : submenu.label;
    size_t underscore = item->text.find('_');
    if (underscore != std::string::npos)
    {
        item->accelerator = (int)underscore;
    }

    item->submenu = &submenu;
    parent_menu.items.push_back(std::move(item));
    return parent_menu.items.back().get();
}

/* Forward declaration for cycle checks + recursion. */
void fill_menu(Menu *parent, xmlNode *node, MenuSet &set);

/* Parse the children of an inline menu definition into @menu. */
void fill_menu_children(Menu &menu, xmlNode *parent_node, MenuSet &set)
{
    for (xmlNode *child = parent_node->children; child; child = child->next)
    {
        if (child->type != XML_ELEMENT_NODE)
        {
            continue;
        }

        if (xmlStrcmp(child->name, (const xmlChar *)"menu") == 0)
        {
            fill_menu(&menu, child, set);
        }
        else if (xmlStrcmp(child->name, (const xmlChar *)"item") == 0)
        {
            auto item  = std::make_unique<MenuItem>();
            item->text = node_attr(child, "label");
            item->icon = node_attr(child, "icon");
            size_t underscore = item->text.find('_');
            if (underscore != std::string::npos)
            {
                item->accelerator = (int)underscore;
            }

            parse_actions(child, item->actions);
            item->id = set.next_item_id();
            menu.items.push_back(std::move(item));
        }
        else if (xmlStrcmp(child->name, (const xmlChar *)"separator") == 0)
        {
            auto item  = std::make_unique<MenuItem>();
            item->type = ItemType::Separator;
            item->text = node_attr(child, "label");
            item->selectable = false;
            item->id = set.next_item_id();
            menu.items.push_back(std::move(item));
        }
        else
        {
            fprintf(stderr, "[menu-parse] unknown element <%s> in menu '%s'\n",
                (const char *)child->name, menu.id.c_str());
        }
    }
}

/* labwc fill_menu: handles the three <menu> roles. */
void fill_menu(Menu *parent, xmlNode *node, MenuSet &set)
{
    std::string id     = node_attr(node, "id");
    std::string label  = node_attr(node, "label");
    std::string execute = node_attr(node, "execute");

    /* Role (a): execute + label → pipemenu definition. */
    if (!execute.empty() && !label.empty())
    {
        Menu *pipe_menu = set.create(id, label);
        if (!pipe_menu)
        {
            fprintf(stderr, "[menu-parse] duplicate menu id '%s'\n", id.c_str());
            return;
        }

        pipe_menu->execute = execute;
        if (parent)
        {
            add_arrow_item(*parent, *pipe_menu);
        }

        return;
    }

    /* Role (b): (label && parent) || !parent → inline definition.
     * NOTE: top-level <menu> elements are ALWAYS definitions (label
     * optional) — this is how root-menu/client-menu are defined without
     * labels in labwc's docs/menu.xml. */
    if ((!label.empty() && parent) || !parent)
    {
        Menu *menu = set.create(id, label);
        if (!menu)
        {
            fprintf(stderr, "[menu-parse] duplicate menu id '%s'\n", id.c_str());
            return;
        }

        fill_menu_children(*menu, node, set);
        if (parent)
        {
            add_arrow_item(*parent, *menu);
        }

        return;
    }

    /* Role (c): id-only → reference to an existing menu. */
    Menu *menu = set.find(id);
    if (!menu)
    {
        fprintf(stderr, "[menu-parse] reference to unknown menu '%s'\n", id.c_str());
        return;
    }

    if (parent)
    {
        add_arrow_item(*parent, *menu);
    }
}
} // namespace

Menu *MenuSet::find(const std::string &id) const
{
    auto it = menus.find(id);
    return it == menus.end() ? nullptr : it->second.get();
}

Menu *MenuSet::create(const std::string &id, const std::string &label)
{
    if (find(id))
    {
        return nullptr; /* ids must be unique (labwc menu_create) */
    }

    auto menu    = std::make_unique<Menu>();
    menu->id     = id;
    menu->label  = label;
    Menu *result = menu.get();
    menus[id]    = std::move(menu);
    return result;
}

void add_default_menus(MenuSet &set)
{
    if (!set.find("root-menu"))
    {
        Menu *root = set.create("root-menu", "");
        auto add   = [&](const std::string &text, Action action)
        {
            auto item  = std::make_unique<MenuItem>();
            item->text = text;
            item->actions.push_back(action);
            item->id = set.next_item_id();
            root->items.push_back(std::move(item));
        };

        add("Terminal", {"Execute", {{"command", "kitty"}}});
        auto sep   = std::make_unique<MenuItem>();
        sep->type  = ItemType::Separator;
        sep->selectable = false;
        sep->id    = set.next_item_id();
        root->items.push_back(std::move(sep));
        add("Reconfigure", {"Reconfigure", {}});
        add("Exit", {"Exit", {}});
    }

    if (!set.find("client-menu"))
    {
        Menu *client = set.create("client-menu", "");
        auto add     = [&](const std::string &text, Action action)
        {
            auto item  = std::make_unique<MenuItem>();
            item->text = text;
            item->actions.push_back(action);
            item->id = set.next_item_id();
            client->items.push_back(std::move(item));
        };

        add("Minimize", {"Iconify", {}});
        add("Maximize", {"ToggleMaximize", {}});
        add("Fullscreen", {"ToggleFullscreen", {}});
        add("Close", {"Close", {}});
    }

    if (!set.find("client-list-combined-menu"))
    {
        set.create("client-list-combined-menu", "");
    }

    if (!set.find("client-send-to-menu"))
    {
        set.create("client-send-to-menu", "");
    }
}

void parse_menu_buffer(const char *data, size_t len, MenuSet &set)
{
    g_set = &set;
    int options = 0;
    xmlDoc *doc = xmlReadMemory(data, (int)len, NULL, NULL, options);
    if (!doc)
    {
        fprintf(stderr, "[menu-parse] xmlReadMemory failed\n");
        return;
    }

    xmlNode *root = xmlDocGetRootElement(doc);
    /* walk root children: top-level <menu> definitions */
    for (xmlNode *child = root->children; child; child = child->next)
    {
        if ((child->type == XML_ELEMENT_NODE) &&
            (xmlStrcmp(child->name, (const xmlChar *)"menu") == 0))
        {
            fill_menu(nullptr, child, set);
        }
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();
    g_set = nullptr;
}

bool parse_menu_file(const std::string &path, MenuSet &set)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    if (content.empty())
    {
        return false;
    }

    fprintf(stderr, "[ob-menu] reading menu file %s\n", path.c_str());
    parse_menu_buffer(content.data(), content.size(), set);
    return true;
}
} // namespace obmenu
