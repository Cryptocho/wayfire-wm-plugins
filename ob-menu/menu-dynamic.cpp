/* SPDX-License-Identifier: GPL-2.0-only */
/* menu-dynamic — rebuild labwc's predefined dynamic menus from the live
 * view list. Called right before a menu tree opens (the open stack is
 * empty then, so no OpenLevel holds pointers into the rebuilt items). */
#include "menu-model.hpp"

#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/output.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/debug.hpp>

#include <set>

namespace obmenu
{
/* label for a client-list entry: title, falling back to app-id */
static std::string view_label(wayfire_toplevel_view view)
{
    auto title = view->get_title();
    if (!title.empty())
    {
        return title;
    }

    return view->get_app_id();
}

/* One item per toplevel view (all workspaces, deduped), each running
 * Activate. Mirrors labwc client-list-combined-menu. */
static void refresh_client_list(MenuSet &set, Menu *menu)
{
    menu->items.clear();
    std::set<wayfire_toplevel_view> seen;
    for (auto *output : wf::get_core().output_layout->get_outputs())
    {
        if (!output->wset())
        {
            continue;
        }

        for (auto &view : output->wset()->get_views())
        {
            if (!seen.insert(view).second)
            {
                continue;
            }

            auto item  = std::make_unique<MenuItem>();
            item->text = view_label(view);
            item->actions.push_back(Action{"Activate", {}});
            item->id = set.next_item_id();
            menu->items.push_back(std::move(item));
        }
    }
}

/* One item per workspace: SendToDesktop to=<1-based row-major index>.
 * On a 1x1 grid the items stay empty (nothing to send to). Mirrors
 * labwc client-send-to-menu (workspace names not modeled → "WS N"). */
static void refresh_client_send_to(MenuSet &set, wf::output_t *output,
    Menu *menu)
{
    menu->items.clear();
    auto wset = output->wset();
    if (!wset)
    {
        return;
    }

    auto grid = wset->get_workspace_grid_size();
    for (int y = 0; y < (int)grid.height; y++)
    {
        for (int x = 0; x < (int)grid.width; x++)
        {
            int n = y * (int)grid.width + x + 1;
            auto item = std::make_unique<MenuItem>();
            item->text = "WS " + std::to_string(n);
            item->actions.push_back(Action{"SendToDesktop",
                {{"to", std::to_string(n)}}});
            item->id = set.next_item_id();
            menu->items.push_back(std::move(item));
        }
    }
}

void refresh_dynamic_menus(MenuSet &set, wf::output_t *output)
{
    if (auto *m = set.find(DYN_MENU_CLIENT_LIST))
    {
        refresh_client_list(set, m);
    }

    if (auto *m = set.find(DYN_MENU_CLIENT_SEND_TO))
    {
        refresh_client_send_to(set, output, m);
    }
}
} // namespace obmenu
