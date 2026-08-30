/* SPDX-License-Identifier: GPL-2.0-only */
/* ob-menu-events — the core-level signal tying ob-decor (emitter) to
 * ob-menu (consumer). Emitted on wf::output_t so both plugins stay
 * independent .so's with no link-time dependency. */
#pragma once

#include <wayfire/view.hpp>
#include <wayfire/geometry.hpp>

#include <string>

namespace obmenu
{
/** Request to open a menu. @anchor is output-local (menu top-left). */
struct ob_menu_request_signal
{
    std::string menu_id;         /* e.g. "client-menu" */
    wf::pointf_t anchor;         /* output-local */
    wayfire_view client_view;    /* menu target; null for root menus */
};
} // namespace obmenu
