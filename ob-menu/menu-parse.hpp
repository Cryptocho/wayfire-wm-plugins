/* SPDX-License-Identifier: GPL-2.0-only */
/* menu-parse — openbox menu.xml → obmenu::MenuSet (mirrors labwc fill_menu
 * semantics; see labwc@5aaa5da0571b src/menu/menu.c). */
#pragma once

#include <string>

#include "menu-model.hpp"

namespace obmenu
{

/* Parse an openbox menu.xml from a file. Missing file is not an error —
 * returns false and leaves the set untouched (caller adds defaults). */
bool parse_menu_file(const std::string &path, MenuSet &set);

/* Parse a menu.xml buffer (used for pipemenu output). */
void parse_menu_buffer(const char *data, size_t len, MenuSet &set);

/* labwc parse_buf(parent, buf): fill the children of a pipemenu script's
 * output root element into @target. While parsing, id-only <menu>
 * references are rejected (they could loop back to static menus) and all
 * newly created menus are flagged is_pipemenu_child. */
void parse_pipe_menu_buffer(const char *data, size_t len, Menu *target,
    MenuSet &set);

} // namespace obmenu
