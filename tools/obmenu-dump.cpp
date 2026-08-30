/* SPDX-License-Identifier: GPL-2.0-only */
/* obmenu-dump — parse a menu.xml and print/assert the resulting tree.
 * Mirrors tools/obtheme-dump.c usage: --menu-file X --assert-file F. */
#include "menu-model.hpp"
#include "menu-parse.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

using namespace obmenu;

static std::vector<std::pair<std::string, std::string>> values;

static void set_value(const std::string &key, const std::string &val)
{
    values.push_back({key, val});
    printf("%-46s = %s\n", key.c_str(), val.c_str());
}

static const char *type_name(ItemType t)
{
    switch (t)
    {
      case ItemType::Item:
          return "item";
      case ItemType::Separator:
          return "separator";
      case ItemType::Title:
          return "title";
    }

    return "?";
}

static void dump_menu(MenuSet &set, Menu &menu)
{
    char key[128];
    snprintf(key, sizeof(key), "menu.%s.exists", menu.id.c_str());
    set_value(key, "true");
    snprintf(key, sizeof(key), "menu.%s.items", menu.id.c_str());
    set_value(key, std::to_string(menu.items.size()));
    if (!menu.execute.empty())
    {
        snprintf(key, sizeof(key), "menu.%s.execute", menu.id.c_str());
        set_value(key, menu.execute);
    }

    int index = 0;
    for (auto &item : menu.items)
    {
        snprintf(key, sizeof(key), "menu.%s.item.%d.type", menu.id.c_str(),
            index);
        set_value(key, type_name(item->type));
        if (!item->text.empty())
        {
            snprintf(key, sizeof(key), "menu.%s.item.%d.text", menu.id.c_str(),
                index);
            set_value(key, item->text);
        }

        if (!item->actions.empty())
        {
            std::string actions;
            for (size_t i = 0; i < item->actions.size(); i++)
            {
                actions += item->actions[i].name;
                if (i + 1 < item->actions.size())
                {
                    actions += ",";
                }
            }

            snprintf(key, sizeof(key), "menu.%s.item.%d.actions",
                menu.id.c_str(), index);
            set_value(key, actions);
            /* first action's command attribute (for Execute assertions) */
            if (!item->actions[0].attrs["command"].empty())
            {
                snprintf(key, sizeof(key), "menu.%s.item.%d.command",
                    menu.id.c_str(), index);
                set_value(key, item->actions[0].attrs["command"]);
            }
        }

        snprintf(key, sizeof(key), "menu.%s.item.%d.submenu", menu.id.c_str(),
            index);
        set_value(key, item->submenu ? item->submenu->id : "none");
        index++;
    }

    /* recurse into inline submenus (not pipemenu placeholders) */
    for (auto &item : menu.items)
    {
        if (item->submenu && item->submenu->execute.empty())
        {
            dump_menu(set, *item->submenu);
        }
    }
}

int main(int argc, char **argv)
{
    const char *menu_file = NULL;
    const char *assert_file = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--menu-file") && i + 1 < argc)
        {
            menu_file = argv[++i];
        }
        else if (!strcmp(argv[i], "--assert-file") && i + 1 < argc)
        {
            assert_file = argv[++i];
        }
        else
        {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return 2;
        }
    }

    MenuSet set;
    bool have_file = menu_file && parse_menu_file(menu_file, set);
    add_default_menus(set);
    (void)have_file;

    printf("menus total: %zu\n", set.menus.size());
    set_value("menus_total", std::to_string(set.menus.size()));
    for (auto &[id, menu] : set.menus)
    {
        (void)id;
        dump_menu(set, *menu);
    }

    if (!assert_file)
    {
        return 0;
    }

    FILE *f = fopen(assert_file, "r");
    if (!f)
    {
        fprintf(stderr, "cannot open assert file %s\n", assert_file);
        return 2;
    }

    int failures = 0, line_nr = 0;
    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        line_nr++;
        /* whole-line comments only */
        const char *scan = line;
        while (*scan == ' ' || *scan == '\t')
        {
            scan++;
        }

        if ((*scan == '#') || (*scan == '\n') || (*scan == '\0'))
        {
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq)
        {
            continue;
        }

        *eq = '\0';
        char *key = line;
        char *want = eq + 1;
        char *end = key + strlen(key);
        while ((end > key) &&
            ((end[-1] == ' ') || (end[-1] == '\t')))
        {
            *--end = '\0';
        }

        while (*want == ' ')
        {
            want++;
        }

        end = want + strlen(want);
        while ((end > want) && ((end[-1] == ' ') || (end[-1] == '\t') ||
               (end[-1] == '\n')))
        {
            *--end = '\0';
        }

        int found = 0;
        for (auto &[k, v] : values)
        {
            if (!strcasecmp(k.c_str(), key))
            {
                found = 1;
                if (strcasecmp(v.c_str(), want))
                {
                    printf("ASSERT FAIL line %d: %s: got %s, want %s\n",
                        line_nr, key, v.c_str(), want);
                    failures++;
                }
                else
                {
                    printf("assert ok line %d: %s == %s\n", line_nr, key,
                        want);
                }

                break;
            }
        }

        if (!found)
        {
            printf("ASSERT FAIL line %d: no such key %s\n", line_nr, key);
            failures++;
        }
    }

    fclose(f);
    if (failures)
    {
        fprintf(stderr, "%d assertion(s) failed\n", failures);
        return 1;
    }

    printf("all menu assertions passed\n");
    return 0;
}
