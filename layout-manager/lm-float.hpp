/* SPDX-License-Identifier: GPL-2.0-only */
/* Per-view layout state for layout-manager. The custom data lives on the
 * view object, so the state persists across mode switches and across
 * wset moves for the lifetime of the view. */
#pragma once

#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/object.hpp>

namespace lm
{
class lm_view_state_t : public wf::custom_data_t
{
  public:
    /** The view is currently floated by the user (or by auto-float rules) */
    bool floating = false;

    /** Last geometry the view had while floated (restored on re-float) */
    bool has_saved = false;
    wf::geometry_t saved_geom = {0, 0, 0, 0};

    /** Geometry the view had before it was collected into the tiling tree;
     *  restored when the mode switches back to stacking. */
    bool has_pre = false;
    wf::geometry_t pre_tiling = {0, 0, 0, 0};

    static lm_view_state_t& get(wayfire_toplevel_view view)
    {
        if (!view->has_data<lm_view_state_t>())
        {
            view->store_data(std::make_unique<lm_view_state_t>());
        }

        return *view->get_data<lm_view_state_t>();
    }

    static bool is_floating(wayfire_toplevel_view view)
    {
        return view->has_data<lm_view_state_t>() &&
               view->get_data<lm_view_state_t>()->floating;
    }
};
}
