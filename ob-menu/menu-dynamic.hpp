/* SPDX-License-Identifier: GPL-2.0-only */
/* menu-dynamic — rebuild labwc's dynamic menus (client list / send-to). */
#pragma once

#include "menu-model.hpp"

namespace wf
{
class output_t;
}

namespace obmenu
{
/** Rebuild client-list-combined-menu and client-send-to-menu for the
 * output the menu is about to open on. Call with an EMPTY open stack. */
void refresh_dynamic_menus(MenuSet &set, wf::output_t *output);
} // namespace obmenu
