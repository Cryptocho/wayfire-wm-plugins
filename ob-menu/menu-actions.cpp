/* SPDX-License-Identifier: GPL-2.0-only */
/* menu-actions — implementation of the labwc action whitelist (M5). */
#include "menu-actions.hpp"

#include <wayfire/core.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/debug.hpp>

#include <cstdlib>
#include <optional>

namespace obmenu
{
/* Resolve a labwc GoToDesktop/SendToDesktop "to" attribute:
 * current (default) / left / right / up / down / 1-based row-major index.
 * Clamped to the workspace grid; nullopt when out of bounds. */
static std::optional<wf::point_t> resolve_desktop(wf::output_t *output,
    const std::string &to)
{
    auto wset = output->wset();
    if (!wset)
    {
        return std::nullopt;
    }

    auto grid = wset->get_workspace_grid_size();
    auto cur  = wset->get_current_workspace();
    wf::point_t target = cur;

    if (to.empty() || (to == "current"))
    {
        return cur;
    }
    else if (to == "left")
    {
        target.x = std::max(cur.x - 1, 0);
    }
    else if (to == "right")
    {
        target.x = std::min(cur.x + 1, (int)grid.width - 1);
    }
    else if (to == "up")
    {
        target.y = std::max(cur.y - 1, 0);
    }
    else if (to == "down")
    {
        target.y = std::min(cur.y + 1, (int)grid.height - 1);
    }
    else
    {
        /* numeric: 1-based, row-major */
        int n = std::atoi(to.c_str());
        if (n >= 1)
        {
            target.x = (n - 1) % (int)grid.width;
            target.y = (n - 1) / (int)grid.width;
        }
    }

    if ((target.x < 0) || (target.y < 0) ||
        (target.x >= (int)grid.width) || (target.y >= (int)grid.height))
    {
        return std::nullopt;
    }

    return target;
}

void run_menu_actions(wf::output_t *output, MenuItem *item,
    wayfire_toplevel_view client_view)
{
    for (auto &action : item->actions)
    {
        const std::string &name = action.name;
        const auto attr = [&] (const char *k) -> std::string
        {
            auto it = action.attrs.find(k);
            return it == action.attrs.end() ? std::string{} : it->second;
        };

        if (name == "Execute")
        {
            auto cmd = attr("command");
            if (!cmd.empty())
            {
                wf::get_core().run(cmd);
            }
        }
        else if (name == "Exit")
        {
            wf::get_core().shutdown();
        }
        else if (name == "Iconify")
        {
            if (client_view)
            {
                wf::get_core().default_wm->minimize_request(client_view, true);
            }
        }
        else if (name == "ToggleMaximize")
        {
            if (client_view)
            {
                wf::get_core().default_wm->tile_request(client_view,
                    client_view->pending_tiled_edges() ? 0 :
                    wf::TILED_EDGES_ALL);
            }
        }
        else if (name == "ToggleFullscreen")
        {
            if (client_view)
            {
                wf::get_core().default_wm->fullscreen_request(client_view,
                    client_view->get_output(),
                    !client_view->pending_fullscreen());
            }
        }
        else if (name == "Close")
        {
            if (client_view)
            {
                client_view->close();
            }
        }
        else if ((name == "Activate") || (name == "Focus"))
        {
            if (client_view)
            {
                wf::get_core().default_wm->focus_raise_view(client_view);
            }
        }
        else if (name == "GoToDesktop")
        {
            if (auto target = resolve_desktop(output, attr("to")))
            {
                output->wset()->set_workspace(*target);
            }
        }
        else if (name == "SendToDesktop")
        {
            if (client_view)
            {
                if (auto target = resolve_desktop(output, attr("to")))
                {
                    output->wset()->move_to_workspace(client_view, *target);
                    /* labwc: follow="yes" (default) switches too */
                    if (attr("follow") != "no")
                    {
                        output->wset()->set_workspace(*target);
                    }
                }
            }
        }
        else if (name == "Reconfigure")
        {
            /* wayfire reloads config on demand; menu/theme reload is part
             * of plugin restart for now */
            LOGI("ob-menu: Reconfigure requested (noop)");
        }
        else
        {
            LOGE("ob-menu: action '", name, "' is not supported (ignored)");
        }
    }
}
} // namespace obmenu
