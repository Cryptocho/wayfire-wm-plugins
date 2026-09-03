/* SPDX-License-Identifier: GPL-2.0-only */
/* ob-theme-data — cross-plugin data object tying the ob-theme provider
 * plugin to its consumers (ob-decor, ob-menu, ob-switcher, ...).
 *
 * The provider stores it on CORE under the name "ob-theme":
 *   wf::get_core().store_data(std::make_unique<ob_theme_data_t>(), "ob-theme")
 * Consumers fetch it with:
 *   wf::get_core().get_data<obtheme::ob_theme_data_t>("ob-theme")
 * A null result means the provider is not loaded — consumers fall back to
 * their own lazy ob_theme_init() (idempotent first-wins in libobtheme).
 *
 * Reload notifications are emitted on CORE as ob_theme_reloaded_signal.
 * The theme struct is updated IN PLACE (ob_theme_reinit), so borrowed
 * pointers stay valid — consumers only need to re-render / drop caches.
 *
 * Pure header: no link-time dependency on the provider plugin (mirrors
 * ob-menu-events.hpp).
 */
#pragma once

#include <wayfire/object.hpp>

#include <obtheme.h>

#include <cstdint>

namespace obtheme
{
/** Emitted on wf::core after an in-place theme reload. */
struct ob_theme_reloaded_signal
{
    uint32_t epoch;
};

/** Provider data: the live theme instance + a monotonic reload epoch. */
struct ob_theme_data_t : public wf::custom_data_t
{
    /* Borrowed pointer, lives for the whole process (providers never
     * finish the theme; see ob-theme.cpp fini()). */
    struct theme *theme = nullptr;
    /* Bumped on every in-place reload; lets consumers cheaply detect
     * changes (e.g. cache invalidation keyed by epoch). */
    uint32_t epoch = 1;
};
} // namespace obtheme
