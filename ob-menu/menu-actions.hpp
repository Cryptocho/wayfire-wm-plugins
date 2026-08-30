/* SPDX-License-Identifier: GPL-2.0-only */
/* menu-actions — labwc action whitelist → wayfire calls.
 * The menu is closed BEFORE these run (callers guarantee it) so keyboard
 * focus returns to the view first. Actions outside the whitelist are
 * logged and ignored (see plan: out of scope). */
#pragma once

#include "menu-model.hpp"

#include <wayfire/output.hpp>
#include <wayfire/toplevel-view.hpp>

namespace obmenu
{
/** Run all actions attached to @item. @client_view is the view the menu
 * was opened for (client-menu); may be null for root menus. */
void run_menu_actions(wf::output_t *output, MenuItem *item,
    wayfire_toplevel_view client_view);
} // namespace obmenu
