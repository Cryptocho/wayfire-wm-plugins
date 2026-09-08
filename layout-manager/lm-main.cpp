/* SPDX-License-Identifier: GPL-2.0-only */
/* layout-manager — global layout mode switching: stacking (native floating)
 * <-> sway-style H/V tiling, plus per-view float.
 *
 * The tiling tree engine is derived from Wayfire plugins/tile (GPL-2.0-only,
 * (c) Wayfire developers); see lm-tree/lm-wset/lm-ipc headers. */
#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/bindings-repository.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/plugins/common/input-grab.hpp>
#include <linux/input-event-codes.h>

#include <algorithm>
#include <string>

#include "lm-tree.hpp"
#include "lm-wset.hpp"
#include "lm-float.hpp"
#include "lm-ipc.hpp"
#include "lm-animator.hpp"
#include "lm-drag.hpp"

namespace lm
{
enum class layout_mode_t
{
    STACKING,
    TILING,
};

static const char *mode_name(layout_mode_t m)
{
    return m == layout_mode_t::TILING ? "tiling" : "stacking";
}

static bool mode_from_string(const std::string &s, layout_mode_t &out)
{
    if (s == "tiling")
    {
        out = layout_mode_t::TILING;
        return true;
    }

    if (s == "stacking")
    {
        out = layout_mode_t::STACKING;
        return true;
    }

    return false;
}

/** Tile-plugin can_tile_view: dialogs (views with a parent) and views with
 * a fixed min==max size are never tiled. */
static bool can_tile_view(wayfire_toplevel_view view)
{
    if (view->parent)
    {
        return false;
    }

    if ((view->toplevel()->get_min_size() == view->toplevel()->get_max_size()) &&
        (view->toplevel()->get_min_size().width > 0) &&
        (view->toplevel()->get_min_size().height > 0))
    {
        return false;
    }

    return true;
}

class wayfire_layout_manager;

/* Per-output instance: bindings and output-scoped signals. The mode they
 * control is global (a single shared layout mode across all outputs). */
class lm_output_t : public wf::per_output_plugin_instance_t
{
  public:
    wayfire_layout_manager *plugin = nullptr;

    void init() override;
    void fini() override;

    /* Defined after wayfire_layout_manager is complete. */
    bool handle_toggle_mode();
    bool tiling_active();
    bool want_tile(wayfire_toplevel_view view);
    bool handle_toggle_float();
    bool handle_tile_move();
    bool handle_tile_resize();
    void handle_view_mapped(wayfire_toplevel_view toplevel);
    bool handle_split(bool horizontal);
    void start_divider_drag(const lm::divider_band_t& band, int axis_pos);
    void stop_divider_drag();
    /* Advance the drag by an axis-relative delta (driven directly by the
     * core motion listener, bypassing grab routing — the implicit button
     * grab that normally anchors pointer motion is never established
     * because the initial press is swallowed). */
    void drag_motion(int axis_delta);
    void drag_motion_raw(double dx, double dy);
    bool divider_drag_active() const
    {
        return drag.active;
    }

    /* The output instance currently running a drag-to-reorder, if any. */
    lm_output_t *find_reorder_output();

    /* Drag-to-reorder (sway-style): press the move button on a tiled view
     * and drag; a preview shows where the view would be inserted on drop
     * (insert/swap). Uses the shared move_drag::core_drag_t (same as the
     * move plugin) so the window follows the cursor while dragging. */
    bool move_drag_active() const
    {
        return reorder_active;
    }

    bool start_move_drag(wayfire_toplevel_view view);
    /** End a drag-to-reorder. @suppress_drop = true when the grab was
     * stolen (no drop should run). */
    void finish_move_drag(bool suppress_drop);
    /** Forward the current pointer position to the shared drag helper. */
    void move_drag_motion(double dx, double dy);

    wf::plugin_activation_data_t grab_interface = {
        .name = "layout-manager",
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
        .cancel = [=]
        {
            stop_divider_drag();
            finish_move_drag(true);
        },
    };

  private:
    /* Divider drag state: adjusts the fractions of two adjacent children
     * of one split node. */
    struct
    {
        bool active = false;
        nonstd::observer_ptr<lm::split_node_t> split;
        lm::split_direction_t axis = lm::SPLIT_VERTICAL;
        int index = 0;
        int start_pos  = 0;
        int cur_pos    = 0;
        double start_f0 = 0.0;
        double avail    = 1.0;
        int32_t splittable = 1;
    } drag;

    std::unique_ptr<wf::input_grab_t> input_grab;

    /* sway MIN_SANE_W/H (include/sway/tree/node.h:8-9) */
    static constexpr int MIN_SANE_W = 100;
    static constexpr int MIN_SANE_H = 60;

    /* True while a drag-to-reorder is in progress on this output. The window
     * motion is displayed with a 2D transformer (the wayfire move_drag
     * static library cannot be linked from an out-of-tree plugin), and the
     * release is routed through on_divider_button (raw core signal).
     *
     * Pointer positions are tracked via event deltas (like the divider
     * drag): the core motion signal fires BEFORE wlr_cursor updates the
     * cursor, and our IGNORE prevents that update entirely. */
    bool reorder_active = false;
    wayfire_toplevel_view reorder_view = nullptr;
    wf::point_t reorder_grab = {0, 0};
    wf::point_t reorder_pos  = {0, 0};
    nonstd::observer_ptr<lm::lm_scale_transformer_t> reorder_tr;

  private:
    wf::option_wrapper_t<wf::activatorbinding_t> key_toggle_mode{
        "layout-manager/key_toggle_mode"};
    wf::option_wrapper_t<wf::activatorbinding_t> key_toggle_float{
        "layout-manager/key_toggle_float"};
    wf::option_wrapper_t<wf::activatorbinding_t> key_split_horizontal{
        "layout-manager/key_split_horizontal"};
    wf::option_wrapper_t<wf::activatorbinding_t> key_split_vertical{
        "layout-manager/key_split_vertical"};
    wf::option_wrapper_t<wf::buttonbinding_t> button_tile_move{
        "layout-manager/button_tile_move"};
    wf::option_wrapper_t<wf::buttonbinding_t> button_tile_resize{
        "layout-manager/button_tile_resize"};
    wf::activator_callback on_split_horizontal = [=] (auto)
    {
        return handle_split(true);
    };
    wf::activator_callback on_split_vertical = [=] (auto)
    {
        return handle_split(false);
    };
    wf::activator_callback on_toggle_mode = [=] (auto)
    {
        return handle_toggle_mode();
    };
    wf::activator_callback on_toggle_float = [=] (auto)
    {
        return handle_toggle_float();
    };
    wf::button_callback on_tile_move = [=] (auto)
    {
        return handle_tile_move();
    };
    wf::button_callback on_tile_resize = [=] (auto)
    {
        return handle_tile_resize();
    };

    void attach_view(wayfire_toplevel_view view, std::optional<wf::point_t> vp = {})
    {
        if (!view->get_wset())
        {
            return;
        }

        lm_workspace_set_data_t::get(view->get_wset()).attach_view(view, vp);
    }

    void detach_view(wayfire_toplevel_view view, bool reinsert = true)
    {
        if (auto node = lm::view_node_t::get_node(view))
        {
            lm_workspace_set_data_t::get(view->get_wset()).detach_views({node}, reinsert);
        }
    }

    /* Only tiled-worthy views are collected/attached; floated views (user
     * float or auto-float rules) stay out of the tree. */
    bool should_tile(wayfire_toplevel_view view);

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped =
        [=] (wf::view_mapped_signal *ev)
    {
        if (auto toplevel = wf::toplevel_cast(ev->view))
        {
            handle_view_mapped(toplevel);
        }
    };

    wf::signal::connection_t<wf::view_tile_request_signal> on_tile_request =
        [=] (wf::view_tile_request_signal *ev)
    {
        if (ev->carried_out || !lm::view_node_t::get_node(ev->view))
        {
            return;
        }

        // we ignore those requests because we manage the tiled state manually
        ev->carried_out = true;
    };

    wf::signal::connection_t<wf::view_fullscreen_request_signal>
    on_fullscreen_request = [=] (wf::view_fullscreen_request_signal *ev)
    {
        if (ev->carried_out || !lm::view_node_t::get_node(ev->view))
        {
            return;
        }

        ev->carried_out = true;
        lm_workspace_set_data_t::get(ev->view->get_wset()).set_view_fullscreen(
            ev->view, ev->state);
    };

    void change_view_workspace(wayfire_toplevel_view view,
        std::optional<wf::point_t> vp = {})
    {
        auto existing_node = lm::view_node_t::get_node(view);
        if (existing_node)
        {
            detach_view(view);
            attach_view(view, vp);
        }
    }

    wf::signal::connection_t<wf::view_change_workspace_signal>
    on_view_change_workspace = [=] (wf::view_change_workspace_signal *ev)
    {
        if (ev->old_workspace_valid)
        {
            change_view_workspace(ev->view, ev->to);
        }
    };

    wf::signal::connection_t<wf::view_minimized_signal> on_view_minimized =
        [=] (wf::view_minimized_signal *ev)
    {
        auto existing_node = lm::view_node_t::get_node(ev->view);

        if (ev->view->minimized && existing_node)
        {
            detach_view(ev->view);
        }

        if (!ev->view->minimized && tiling_active() && want_tile(ev->view))
        {
            attach_view(ev->view);
        }
    };
};

class wayfire_layout_manager : public wf::plugin_interface_t
{
  public:
    friend class lm_output_t;

    /* Tracks drag-to-reorder drops (shared move_drag helper). One instance
     * for the whole plugin: the core_drag_t singleton is global. */
    std::unique_ptr<lm::drag_manager_t> drag_manager;

    lm_output_t *find_reorder_output()
    {
        return outputs.find_reorder();
    }

    void init() override
    {
        drag_manager = std::make_unique<lm::drag_manager_t>();
        wf::option_wrapper_t<std::string> default_mode{
            "layout-manager/default_mode"};
        std::string dm = default_mode.value();
        if (!mode_from_string(dm, mode))
        {
            LOGE("layout-manager: invalid default_mode '", dm,
                "', falling back to stacking");
            mode = layout_mode_t::STACKING;
        }

        wf::option_wrapper_t<std::string> float_opt{
            "layout-manager/float_by_default"};
        if (!float_opt.value().empty())
        {
            float_matcher =
                std::make_unique<wf::view_matcher_t>("layout-manager/float_by_default");
        }

        outputs.plugin = this;
        outputs.init_output_tracking();

        wf::get_core().connect(&on_view_unmapped);
        wf::get_core().connect(&on_focus_changed);
        wf::get_core().connect(&on_view_pre_moved_to_wset);
        wf::get_core().connect(&on_view_moved_to_wset);
        wf::get_core().connect(&on_divider_button);
        wf::get_core().connect(&on_divider_motion);
        wf::get_core().connect(&on_follow_mouse_motion);

        register_ipc();
        LOGI("layout-manager init: mode=", mode_name(mode));

        if (mode == layout_mode_t::TILING)
        {
            migrate_to_tiling();
        }
    }

    void fini() override
    {
        /* Restore the pre-tiling geometry of every collected view before
         * tearing the trees down (tile does not restore geometry; we do,
         * matching the "switch back to stacking" semantics). */
        if (mode == layout_mode_t::TILING)
        {
            migrate_to_stacking();
        }

        lm::layout_animator_t::get().cancel_all();
        unregister_ipc();
        outputs.fini_output_tracking();
        for (auto wset : wf::workspace_set_t::get_all())
        {
            wset->erase_data<lm_workspace_set_data_t>();
        }
    }

    bool toggle_mode()
    {
        set_mode(mode == layout_mode_t::TILING ? layout_mode_t::STACKING :
            layout_mode_t::TILING);
        return true;
    }

    /* sway split command: wrap the focused tiled view in a new split so
     * that views inserted while it is focused stack in the new direction.
     * horizontal = side by side (tile convention SPLIT_VERTICAL). */
    bool split_focused_view(wayfire_toplevel_view view, bool horizontal)
    {
        if (!is_tiling() || !view || !view->get_wset())
        {
            return false;
        }

        auto &data = lm_workspace_set_data_t::get(view->get_wset());
        return data.split_view(view,
            horizontal ? lm::SPLIT_VERTICAL : lm::SPLIT_HORIZONTAL);
    }

    layout_mode_t get_mode() const
    {
        return mode;
    }

    bool is_tiling() const
    {
        return mode == layout_mode_t::TILING;
    }

    /* Per-view float toggle (sway floating toggle). Only meaningful in
     * tiling mode: a tiled view becomes floating (remembering its float
     * geometry across toggles), a floating view re-enters the tree after
     * the last focused tiled view. */
    bool toggle_float_view(wayfire_toplevel_view view)
    {
        if (!is_tiling() || !view || !view->get_wset())
        {
            return false;
        }

        auto &st   = lm_view_state_t::get(view);
        auto node  = lm::view_node_t::get_node(view);
        auto &data = lm_workspace_set_data_t::get(view->get_wset());

        if (node)
        {
            /* tiled -> floating: back to the wset node, then give it a
             * floating geometry */
            data.detach_views({node});
            st.floating = true;

            auto from = view->get_geometry();
            wf::geometry_t target;
            if (st.has_saved)
            {
                target = st.saved_geom;
            } else
            {
                target = default_float_geometry(view);
                st.has_saved   = true;
                st.saved_geom  = target;
            }

            lm::autocommit_transaction_t tx;
            view->toplevel()->pending().tiled_edges = 0;
            view->toplevel()->pending().geometry    = target;
            tx.tx->add_object(view->toplevel());
            lm::layout_animator_t::get().animate(view, from);
            wf::get_core().seat->focus_view(view);
            LOGI("layout-manager: floated view ", view->get_id());
            return true;
        }

        if (st.floating && can_tile_view(view))
        {
            /* floating -> tiled: remember the float geometry, insert after
             * the last focused tiled view (sway: adopt reference size —
             * the fraction model assigns it a fresh share) */
            st.has_saved   = true;
            st.saved_geom  = view->get_pending_geometry();
            st.pre_tiling  = st.saved_geom;
            st.has_pre     = true;
            st.floating    = false;
            data.attach_view(view);
            wf::get_core().seat->focus_view(view);
            LOGI("layout-manager: tiled floating view ", view->get_id());
            return true;
        }

        return false;
    }

  private:
    /* sway floating_set_default_size semantics (container.c:896-917):
     * half the workarea width, three quarters of its height, clamped to
     * the view's min/max sizes, centered in the workarea. */
    wf::geometry_t default_float_geometry(wayfire_toplevel_view view)
    {
        auto wo = view->get_output();
        auto wa = wo ? wo->workarea->get_workarea() : wf::geometry_t{0, 0, 1280, 720};
        int w = wa.width * 0.5;
        int h = wa.height * 0.75;

        auto min = view->toplevel()->get_min_size();
        auto max = view->toplevel()->get_max_size();
        if (min.width > 0)
        {
            w = std::max(w, min.width);
        }

        if (min.height > 0)
        {
            h = std::max(h, min.height);
        }

        if (max.width > 0)
        {
            w = std::min(w, max.width);
        }

        if (max.height > 0)
        {
            h = std::min(h, max.height);
        }

        return {
            wa.x + (wa.width - w) / 2,
            wa.y + (wa.height - h) / 2,
            w,
            h,
        };
    }

    void set_mode(layout_mode_t m)
    {
        if (m == mode)
        {
            return;
        }

        if (m == layout_mode_t::TILING)
        {
            mode = m;
            migrate_to_tiling();
        } else
        {
            migrate_to_stacking();
            mode = m;
        }

        LOGI("layout-manager: mode -> ", mode_name(mode));
    }

  private:
    layout_mode_t mode = layout_mode_t::STACKING;

    /* Auto-float rules: matching views are never tiled. Constructed only
     * when the option is non-empty (an empty matcher string fails to
     * parse and would spam the log). */
    std::unique_ptr<wf::view_matcher_t> float_matcher;

    /* focus-follows-mouse for tiled views (tiling mode only) */
    wf::option_wrapper_t<bool> follow_mouse{"layout-manager/focus_follows_mouse"};

    /* Collect all currently mapped views of every workspace set into their
     * workspace's tiling tree. */
    void migrate_to_tiling()
    {
        for (auto wo : wf::get_core().output_layout->get_outputs())
        {
            harvest_output(wo);
        }
    }

    void harvest_output(wf::output_t *wo)
    {
        auto wset = wo->wset();
        if (!wset)
        {
            return;
        }

        auto &data = lm_workspace_set_data_t::get(wset);
        auto grid  = wset->get_workspace_grid_size();
        for (int i = 0; i < grid.width; i++)
        {
            for (int j = 0; j < grid.height; j++)
            {
                wf::point_t vp{i, j};
                auto views = wset->get_views(wf::WSET_MAPPED_ONLY, vp);
                std::vector<wayfire_toplevel_view> want;
                for (auto v : views)
                {
                    auto tl = wf::toplevel_cast(v);
                    if (!tl || !should_tile(tl))
                    {
                        continue;
                    }

                    /* A view may span several workspaces: collect it once,
                     * on its main workspace. */
                    if (wset->get_view_main_workspace(tl) != vp)
                    {
                        continue;
                    }

                    want.push_back(tl);
                }

                /* Row-major order (center y, then center x) gives a
                 * deterministic, spatially sensible arrangement. */
                std::sort(want.begin(), want.end(),
                    [] (wayfire_toplevel_view a, wayfire_toplevel_view b)
                {
                    auto ga = a->get_geometry();
                    auto gb = b->get_geometry();
                    int cay = ga.y + ga.height / 2;
                    int cby = gb.y + gb.height / 2;
                    if (cay != cby)
                    {
                        return cay < cby;
                    }

                    return ga.x + ga.width / 2 < gb.x + gb.width / 2;
                });

                for (auto v : want)
                {
                    auto &st = lm_view_state_t::get(v);
                    st.pre_tiling = v->get_pending_geometry();
                    st.has_pre    = true;
                    data.attach_view(v, vp);
                }
            }
        }
    }

    /* Untile every view and restore its pre-tiling geometry. Floating views
     * are not in the trees and are not touched. */
    void migrate_to_stacking()
    {
        for (auto wset : wf::workspace_set_t::get_all())
        {
            if (!wset->has_data<lm_workspace_set_data_t>())
            {
                continue;
            }

            auto &data = *wset->get_data<lm_workspace_set_data_t>();
            std::vector<wayfire_toplevel_view> views;
            for (auto &col : data.roots)
            {
                for (auto &root : col)
                {
                    if (root)
                    {
                        lm::for_each_view(root,
                            [&] (wayfire_toplevel_view v)
                        {
                            views.push_back(v);
                        });
                    }
                }
            }

            if (views.empty())
            {
                continue;
            }

            std::vector<std::pair<wayfire_toplevel_view, wf::geometry_t>> anims;
            {
                lm::autocommit_transaction_t tx;
                for (auto v : views)
                {
                    auto from = v->get_geometry();
                    auto &st = lm_view_state_t::get(v);
                    v->toplevel()->pending().tiled_edges = 0;
                    if (st.has_pre)
                    {
                        v->toplevel()->pending().geometry = st.pre_tiling;
                    }

                    tx.tx->add_object(v->toplevel());
                    anims.push_back({v, from});
                }
            }

            for (auto& [v, from] : anims)
            {
                lm::layout_animator_t::get().animate(v, from);
            }

            std::vector<nonstd::observer_ptr<lm::view_node_t>> nodes;
            for (auto v : views)
            {
                if (auto n = lm::view_node_t::get_node(v))
                {
                    nodes.push_back(n);
                }
            }

            data.detach_views(nodes);
        }
    }

    bool should_tile(wayfire_toplevel_view view)
    {
        if (!is_tiling() || !can_tile_view(view) ||
            lm_view_state_t::is_floating(view))
        {
            return false;
        }

        return !float_matcher || !float_matcher->matches(view);
    }

    /* Auto-float rules: matching views never enter the tree on their own */
    bool auto_float(wayfire_toplevel_view view)
    {
        return is_tiling() && can_tile_view(view) && float_matcher &&
               float_matcher->matches(view);
    }

    /* ---- core-level signal handlers (same split as Wayfire tile) ---- */

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped =
        [=] (wf::view_unmapped_signal *ev)
    {
        auto toplevel = wf::toplevel_cast(ev->view);
        if (!toplevel)
        {
            return;
        }

        lm::layout_animator_t::get().cancel(toplevel);
        if (auto node = lm::view_node_t::get_node(ev->view))
        {
            wf::dassert(toplevel->get_wset() != nullptr);
            auto &data = lm_workspace_set_data_t::get(toplevel->get_wset());
            data.detach_views({node});
        }
    };

    wf::signal::connection_t<wf::keyboard_focus_changed_signal>
    on_focus_changed = [=] (wf::keyboard_focus_changed_signal *ev)
    {
        if (auto toplevel = wf::toplevel_cast(wf::node_to_view(ev->new_focus)))
        {
            if (toplevel->get_wset() &&
                wset_has_data(toplevel->get_wset()))
            {
                auto &data = lm_workspace_set_data_t::get(toplevel->get_wset());
                data.consider_exit_fullscreen(toplevel);
                if (lm::view_node_t::get_node(toplevel))
                {
                    data.last_tiled_view = toplevel;
                }
            }
        }
    };

    static bool wset_has_data(std::shared_ptr<wf::workspace_set_t> wset)
    {
        return wset->has_data<lm_workspace_set_data_t>();
    }

    wf::signal::connection_t<wf::view_pre_moved_to_wset_signal>
    on_view_pre_moved_to_wset = [=] (wf::view_pre_moved_to_wset_signal *ev)
    {
        auto node = lm::view_node_t::get_node(ev->view);
        if (node)
        {
            ev->view->store_data(std::make_unique<lm_keep_state_t>());
            if (ev->old_wset && ev->old_wset->has_data<lm_workspace_set_data_t>())
            {
                lm_workspace_set_data_t::get(ev->old_wset).detach_views({node});
            }
        }
    };

    wf::signal::connection_t<wf::view_moved_to_wset_signal>
    on_view_moved_to_wset = [=] (wf::view_moved_to_wset_signal *ev)
    {
        if (ev->view->has_data<lm_keep_state_t>() && ev->new_wset)
        {
            ev->view->erase_data<lm_keep_state_t>();
            if (is_tiling())
            {
                lm_workspace_set_data_t::get(ev->new_wset).attach_view(ev->view);
            }
        }
    };

    /* Divider drag entry: a BTN_LEFT press inside a divider band starts
     * a fraction drag (the press is swallowed so views/decoration never
     * see it). While dragging, further button events are swallowed too;
     * release stops the drag. Same early-swallow route as the ob-menu
     * desktop right-click.
     *
     * Only presses WITHOUT any pressed modifier start divider/edge drags:
     * modifier-pressed presses are left to the button bindings (the move
     * button -> drag-to-reorder, the resize button -> fraction resize),
     * mirroring the wayfire move/resize plugins which ignore tiled views. */
    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_button_event>>
    on_divider_button = [=] (wf::input_event_signal<wlr_pointer_button_event> *ev)
    {
        if (ev->event->button != BTN_LEFT)
        {
            return;
        }

        if (auto *dragging = outputs.find_active_drag())
        {
            if (ev->event->state == WL_POINTER_BUTTON_STATE_RELEASED)
            {
                dragging->stop_divider_drag();
            }

            ev->mode = wf::input_event_processing_mode_t::IGNORE;
            return;
        }

        /* A drag-to-reorder is in progress somewhere: route the release
         * through the shared drag helper (drop runs on drag_done). */
        if (auto *reordering = find_reorder_output())
        {
            if (ev->event->state == WL_POINTER_BUTTON_STATE_RELEASED)
            {
                reordering->finish_move_drag(false);
            }

            ev->mode = wf::input_event_processing_mode_t::IGNORE;
            return;
        }

        if ((ev->mode != wf::input_event_processing_mode_t::FULL) ||
            (ev->event->state != WL_POINTER_BUTTON_STATE_PRESSED) ||
            !is_tiling())
        {
            return;
        }

        /* modifier held: leave the press to the button bindings */
        if (wf::get_core().seat->get_keyboard_modifiers() != 0)
        {
            return;
        }

        auto gc = wf::get_core().get_cursor_position().round_down();
        auto wo = wf::get_core().output_layout->get_output_at(gc.x, gc.y);
        auto *inst = wo ? outputs.get(wo) : nullptr;
        if (!inst || !wo->wset())
        {
            return;
        }

        auto wset = wo->wset();
        auto &data = lm_workspace_set_data_t::get(wset);
        auto vp = wset->get_current_workspace();
        auto &root = data.roots[vp.x][vp.y];
        if (!root)
        {
            return;
        }

        /* no divider dragging while a tiled view is fullscreen */
        bool fullscreen = false;
        lm::for_each_view(root, [&] (wayfire_toplevel_view v)
        {
            fullscreen |= v->pending_fullscreen();
        });
        if (fullscreen)
        {
            return;
        }

        /* cursor in wset-global coordinates */
        auto og    = wo->get_layout_geometry();
        auto size  = wset->get_last_output_geometry().value_or(
            lm::default_output_resolution);
        wf::point_t wsetg{
            gc.x - og.x + vp.x * size.width,
            gc.y - og.y + vp.y * size.height};

        for (auto &band : lm::get_divider_bands(root))
        {
            if ((wsetg.x >= band.band.x) &&
                (wsetg.x < band.band.x + band.band.width) &&
                (wsetg.y >= band.band.y) &&
                (wsetg.y < band.band.y + band.band.height))
            {
                int axis_pos = (band.direction == lm::SPLIT_VERTICAL) ?
                    wsetg.x : wsetg.y;
                inst->start_divider_drag(band, axis_pos);
                if (inst->divider_drag_active())
                {
                    ev->mode = wf::input_event_processing_mode_t::IGNORE;
                }
                return;
            }
        }

        /* Sway edge-resize: pressing on a tiled view's frame edge adjusts
         * the boundary between that view's subtree and the adjacent one,
         * so resizing a window makes its neighbors give/take space. */
        auto focus = wf::toplevel_cast(wf::get_core().get_cursor_focus_view());
        if (focus && (focus->get_output() == wo))
        {
            auto node = lm::view_node_t::get_node(focus);
            if (node)
            {
                const int EDGE = 6;
                auto edge_band = edge_divider_band(node,
                    wf::point_t{gc.x - og.x, gc.y - og.y}, EDGE, EDGE);
                if (edge_band)
                {
                    int axis_pos = (edge_band->direction ==
                        lm::SPLIT_VERTICAL) ? wsetg.x : wsetg.y;
                    inst->start_divider_drag(*edge_band, axis_pos);
                    if (inst->divider_drag_active())
                    {
                        ev->mode =
                            wf::input_event_processing_mode_t::IGNORE;
                    }
                }
            }
        }
    };

    /**
     * If the wset-local point lies on one of the leaf's frame edges
     * (left/right/top/bottom strip of the given sizes), find the boundary
     * to adjust: the nearest ancestor split with the matching axis that
     * has a sibling on that side (sway resize semantics). The reference
     * box is the view's target geometry (the wm box — the SSD frame lives
     * inside it), which is inset from the raw tree-node box by the gaps.
     */
    static std::optional<lm::divider_band_t> edge_divider_band(
        nonstd::observer_ptr<lm::view_node_t> node, const wf::point_t& p,
        int horiz_edge, int vert_edge)
    {
        const auto &g = node->target_geometry();
        enum { LEFT, RIGHT, TOP, BOTTOM } edge;
        bool at_left   = (p.x >= g.x) && (p.x < g.x + horiz_edge) &&
            (p.y >= g.y) && (p.y < g.y + g.height);
        bool at_right  = (p.x >= g.x + g.width - horiz_edge) && (p.x < g.x + g.width) &&
            (p.y >= g.y) && (p.y < g.y + g.height);
        bool at_top    = (p.y >= g.y) && (p.y < g.y + vert_edge) &&
            (p.x >= g.x) && (p.x < g.x + g.width);
        bool at_bottom = (p.y >= g.y + g.height - vert_edge) && (p.y < g.y + g.height) &&
            (p.x >= g.x) && (p.x < g.x + g.width);

        if (at_left)
        {
            edge = LEFT;
        } else if (at_right)
        {
            edge = RIGHT;
        } else if (at_top)
        {
            edge = TOP;
        } else if (at_bottom)
        {
            edge = BOTTOM;
        } else
        {
            return std::nullopt;
        }

        bool forward = (edge == RIGHT) || (edge == BOTTOM);
        lm::split_direction_t axis = ((edge == LEFT) || (edge == RIGHT)) ?
            lm::SPLIT_VERTICAL : lm::SPLIT_HORIZONTAL;

        /* climb to the nearest ancestor split with a neighbor on that side */
        nonstd::observer_ptr<lm::tree_node_t> cur = node;
        while (cur && cur->parent)
        {
            auto parent = cur->parent;
            if (parent->get_split_direction() == axis)
            {
                for (size_t i = 0; i + 1 < parent->children.size(); i++)
                {
                    if (parent->children[i].get() == cur.get())
                    {
                        if (forward)
                        {
                            return lm::divider_band_t{parent, (int)i,
                                {}, axis};
                        }
                    }
                }

                if (!forward)
                {
                    for (size_t i = 1; i < parent->children.size(); i++)
                    {
                        if (parent->children[i].get() == cur.get())
                        {
                            return lm::divider_band_t{parent, (int)i - 1,
                                {}, axis};
                        }
                    }
                }
            }

            cur = parent;
        }

        return std::nullopt;
    }

    /* While a divider drag is active, motion is driven directly from the
     * device-event signal (the press that started the drag was swallowed,
     * so wayfire's implicit button grab never anchors pointer motion to
     * the grab node). A drag-to-reorder uses the same raw signal to feed
     * the shared move_drag helper. */
    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_motion_event>>
    on_divider_motion = [=] (wf::input_event_signal<wlr_pointer_motion_event> *ev)
    {
        auto *dragging = outputs.find_active_drag();
        if (dragging)
        {
            dragging->drag_motion_raw(ev->event->delta_x, ev->event->delta_y);
            ev->mode = wf::input_event_processing_mode_t::IGNORE;
            return;
        }

        if (auto *reordering = find_reorder_output())
        {
            reordering->move_drag_motion(ev->event->delta_x,
                ev->event->delta_y);
            ev->mode = wf::input_event_processing_mode_t::IGNORE;
        }
    };

    /* Focus-follows-mouse for tiled views: when the pointer moves over a
     * tiled view (either tiling style), focus it. Only in tiling mode and
     * only for tiled views — floating/stacking stay click-to-focus.
     * Uses the POST-event signal: the pre-event signal fires before
     * wayfire updates the cursor focus, so get_cursor_focus_view() would
     * lag one event behind. */
    wf::signal::connection_t<wf::post_input_event_signal<wlr_pointer_motion_event>>
    on_follow_mouse_motion = [=] (wf::post_input_event_signal<wlr_pointer_motion_event> *ev)
    {
        if (!follow_mouse || !is_tiling())
        {
            return;
        }

        if (outputs.find_active_drag())
        {
            return;
        }

        auto focus = wf::toplevel_cast(wf::get_core().get_cursor_focus_view());
        if (!focus || !lm::view_node_t::get_node(focus))
        {
            return; /* floating view / decoration-less gap: untouched */
        }

        auto active = wf::toplevel_cast(wf::get_core().seat->get_active_view());
        if (active != focus)
        {
            wf::get_core().seat->focus_view(focus);
        }
    };

    /* ---- outputs & IPC ---- */

    struct outputs_t : public wf::per_output_tracker_mixin_t<lm_output_t>
    {
        wayfire_layout_manager *plugin = nullptr;

        lm_output_t *get(wf::output_t *output)
        {
            auto it = output_instance.find(output);
            return it == output_instance.end() ? nullptr : it->second.get();
        }

        lm_output_t *find_active_drag()
        {
            for (auto &[wo, inst] : output_instance)
            {
                if (inst->divider_drag_active())
                {
                    return inst.get();
                }
            }

            return nullptr;
        }

        lm_output_t *find_reorder()
        {
            for (auto &[wo, inst] : output_instance)
            {
                if (inst->move_drag_active())
                {
                    return inst.get();
                }
            }

            return nullptr;
        }

      protected:
        void handle_new_output(wf::output_t *output) override
        {
            auto inst = std::make_unique<lm_output_t>();
            inst->output = output;
            inst->plugin  = plugin;
            auto ptr = inst.get();
            output_instance[output] = std::move(inst);
            ptr->init();
        }
    };

    outputs_t outputs;

    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;

    void register_ipc()
    {
        ipc_repo->register_method("layout-manager/get-mode",
            [this] (wf::json_t)
        {
            wf::json_t r;
            r["mode"] = mode_name(mode);
            return r;
        });

        ipc_repo->register_method("layout-manager/set-mode",
            [this] (wf::json_t data)
        {
            wf::json_t r;
            if (!data.has_member("mode"))
            {
                r["error"] = std::string("missing 'mode' member (tiling|stacking)");
                return r;
            }

            layout_mode_t m;
            std::string s = (std::string)data["mode"];
            if (!mode_from_string(s, m))
            {
                r["error"] = "invalid mode '" + s + "' (tiling|stacking)";
                return r;
            }

            set_mode(m);
            r["mode"] = mode_name(mode);
            return r;
        });

        ipc_repo->register_method("layout-manager/toggle-mode",
            [this] (wf::json_t)
        {
            toggle_mode();
            wf::json_t r;
            r["mode"] = mode_name(mode);
            return r;
        });

        ipc_repo->register_method("layout-manager/toggle-float",
            [this] (wf::json_t data)
        {
            wf::json_t r;
            wayfire_toplevel_view view;
            if (data.has_member("id"))
            {
                view = wf::toplevel_cast(
                    wf::ipc::find_view_by_id(wf::ipc::json_get_uint64(data, "id")));
            } else
            {
                view = wf::toplevel_cast(wf::get_core().seat->get_active_view());
            }

            if (!view)
            {
                r["error"] = std::string("no view to toggle");
                return r;
            }

            if (!toggle_float_view(view))
            {
                r["error"] = std::string("toggle not applicable to this view");
                return r;
            }

            r["floating"] = lm_view_state_t::is_floating(view);
            return r;
        });

        ipc_repo->register_method("layout-manager/get-view-state",
            [] (wf::json_t data)
        {
            wf::json_t r;
            if (!data.has_member("id"))
            {
                r["error"] = std::string("missing id");
                return r;
            }

            auto view = wf::toplevel_cast(
                wf::ipc::find_view_by_id(wf::ipc::json_get_uint64(data, "id")));
            if (!view)
            {
                r["error"] = std::string("no such view");
                return r;
            }

            bool has = view->has_data<lm_view_state_t>();
            auto *st = has ? view->get_data<lm_view_state_t>().get() : nullptr;
            r["floating"]  = st && st->floating;
            r["has_saved"] = st && st->has_saved;
            r["has_pre"]   = st && st->has_pre;
            if (st && st->has_saved)
            {
                r["saved_geom"] = wf::ipc::geometry_to_json(st->saved_geom);
            }

            return r;
        });

        ipc_repo->register_method("layout-manager/split",
            [this] (wf::json_t data)
        {
            wf::json_t r;
            wayfire_toplevel_view view;
            if (data.has_member("id"))
            {
                view = wf::toplevel_cast(
                    wf::ipc::find_view_by_id(wf::ipc::json_get_uint64(data, "id")));
            } else
            {
                view = wf::toplevel_cast(wf::get_core().seat->get_active_view());
            }

            if (!view)
            {
                r["error"] = std::string("no view to split");
                return r;
            }

            std::string dir = data.has_member("direction") ?
                (std::string)data["direction"] : "v";
            bool ok = split_focused_view(view, dir == "h");
            if (!ok)
            {
                r["error"] = std::string("view is not tiled");
                return r;
            }

            r["split"] = dir;
            return r;
        });

        ipc_repo->register_method("layout-manager/get-layout",
            [this] (const wf::json_t & params)
        {
            return lm::handle_ipc_get_layout(params);
        });
    }

    void unregister_ipc()
    {
        ipc_repo->unregister_method("layout-manager/get-mode");
        ipc_repo->unregister_method("layout-manager/set-mode");
        ipc_repo->unregister_method("layout-manager/toggle-mode");
        ipc_repo->unregister_method("layout-manager/split");
        ipc_repo->unregister_method("layout-manager/toggle-float");
        ipc_repo->unregister_method("layout-manager/get-view-state");
        ipc_repo->unregister_method("layout-manager/get-layout");
    }
};

bool lm_output_t::should_tile(wayfire_toplevel_view view)
{
    return want_tile(view);
}

bool lm_output_t::tiling_active()
{
    return plugin->is_tiling();
}

bool lm_output_t::want_tile(wayfire_toplevel_view view)
{
    return plugin->should_tile(view);
}

bool lm_output_t::handle_toggle_mode()
{
    return plugin->toggle_mode();
}

void lm_output_t::handle_view_mapped(wayfire_toplevel_view toplevel)
{
    if (!tiling_active())
    {
        return;
    }

    if (want_tile(toplevel))
    {
        attach_view(toplevel);
    } else if (plugin->auto_float(toplevel))
    {
        /* auto-float rules: keep the client-chosen geometry, just
         * mark it as floating so it stays out of the tree */
        lm_view_state_t::get(toplevel).floating = true;
    }
}

bool lm_output_t::handle_split(bool horizontal)
{
    auto view = wf::toplevel_cast(wf::get_core().seat->get_active_view());
    if (!view || (view->get_output() != output))
    {
        return false;
    }

    return plugin->split_focused_view(view, horizontal);
}

void lm_output_t::start_divider_drag(const lm::divider_band_t& band,
    int axis_pos)
{
    if (drag.active || !band.split)
    {
        return;
    }

    if (!output->activate_plugin(&grab_interface))
    {
        return;
    }

    drag.active     = true;
    drag.split      = band.split;
    drag.axis       = band.direction;
    drag.index      = band.index;
    drag.start_pos  = axis_pos;
    drag.cur_pos    = axis_pos;
    drag.start_f0   = band.split->children[band.index]->fraction;
    drag.avail      = drag.start_f0 +
        band.split->children[band.index + 1]->fraction;
    drag.splittable = (band.direction == lm::SPLIT_VERTICAL) ?
        band.split->geometry.width : band.split->geometry.height;
    if (drag.splittable <= 0)
    {
        drag.splittable = 1;
    }

    input_grab->grab_input(wf::scene::layer::OVERLAY);
    LOGI("layout-manager: divider drag started on split ", band.index);
}

void lm_output_t::stop_divider_drag()
{
    if (!drag.active)
    {
        return;
    }

    drag.active = false;
    drag.split  = nullptr;
    if (output->is_plugin_active(grab_interface.name))
    {
        output->deactivate_plugin(&grab_interface);
    }

    input_grab->ungrab_input();
}

void lm_output_t::drag_motion_raw(double dx, double dy)
{
    if (!drag.active)
    {
        return;
    }

    drag_motion((int)std::lround(drag.axis == lm::SPLIT_VERTICAL ? dx : dy));
}

void lm_output_t::drag_motion(int axis_delta)
{
    if (!drag.active || !drag.split || (drag.split->children.size() <
        (size_t)(drag.index + 2)))
    {
        stop_divider_drag();
        return;
    }

    drag.cur_pos += axis_delta;
    int delta = drag.cur_pos - drag.start_pos;

    int min_sane = (drag.axis == lm::SPLIT_VERTICAL) ? MIN_SANE_W : MIN_SANE_H;
    double min_share = (double)min_sane / drag.splittable;
    double f0 = drag.start_f0 + (double)delta / drag.splittable;
    f0 = std::clamp(f0, min_share, drag.avail - min_share);

    drag.split->children[drag.index]->fraction = f0;
    drag.split->children[drag.index + 1]->fraction = drag.avail - f0;

    lm::autocommit_transaction_t tx;
    drag.split->set_geometry(drag.split->geometry, tx.tx);
}

bool lm_output_t::handle_toggle_float()
{
    auto view = wf::toplevel_cast(wf::get_core().seat->get_active_view());
    if (!view || (view->get_output() != output))
    {
        return false;
    }

    return plugin->toggle_float_view(view);
}

lm_output_t *lm_output_t::find_reorder_output()
{
    return plugin->find_reorder_output();
}

/* ---- drag-to-reorder (sway semantics) ---- */

bool lm_output_t::handle_tile_move()
{
    if (!plugin->is_tiling())
    {
        return false;
    }

    /* a divider/edge drag is already in progress */
    if (output->is_plugin_active(grab_interface.name))
    {
        return false;
    }

    auto focus = wf::toplevel_cast(wf::get_core().get_cursor_focus_view());
    if (!focus || !lm::view_node_t::get_node(focus) ||
        (focus->get_output() != output))
    {
        return false; /* not on a tiled view: leave to the move plugin */
    }

    return start_move_drag(focus);
}

bool lm_output_t::start_move_drag(wayfire_toplevel_view view)
{
    if (reorder_active || output->is_plugin_active(grab_interface.name))
    {
        return false; /* a drag (ours or another plugin's) is running */
    }

    if (!output->activate_plugin(&grab_interface))
    {
        return false;
    }

    input_grab->grab_input(wf::scene::layer::OVERLAY);
    reorder_view   = view;
    reorder_grab   = lm::get_global_input_coordinates(output);
    reorder_pos    = reorder_grab;
    reorder_active = true;
    reorder_tr = wf::ensure_named_transformer<lm::lm_scale_transformer_t>(
        view, wf::TRANSFORMER_2D, lm::lm_drag_anim_name, view);
    reorder_tr->set_target_box(view->get_geometry());
    view->damage();
    LOGI("layout-manager: drag-to-reorder started for view ", view->get_id());
    return true;
}

void lm_output_t::finish_move_drag(bool suppress_drop)
{
    if (!reorder_active)
    {
        return;
    }

    reorder_active = false;

    if (reorder_view)
    {
        if (reorder_tr && reorder_view->get_transformed_node())
        {
            reorder_view->get_transformed_node()->rem_transformer(
                lm::lm_drag_anim_name);
            reorder_view->damage();
        }

        if (plugin->drag_manager)
        {
            if (!suppress_drop)
            {
                plugin->drag_manager->handle_drop(reorder_view, output,
                    reorder_pos);
            } else
            {
                plugin->drag_manager->suppress_next_drop();
                plugin->drag_manager->hide_preview();
            }
        }
    }

    reorder_view = nullptr;
    reorder_tr   = nullptr;

    if (output->is_plugin_active(grab_interface.name))
    {
        output->deactivate_plugin(&grab_interface);
    }

    input_grab->ungrab_input();
}

void lm_output_t::move_drag_motion(double dx, double dy)
{
    if (!reorder_active || !reorder_view)
    {
        return;
    }

    /* Track the drag position ourselves: the raw motion signal fires
     * before wlr_cursor updates the cursor, so get_cursor_position() is
     * one event behind (and the IGNORE below stops the update entirely).
     * The grab position was captured at press time when the cursor was
     * already up to date. */
    reorder_pos.x += (int)std::lround(dx);
    reorder_pos.y += (int)std::lround(dy);

    auto g = reorder_view->get_geometry();
    wf::geometry_t box{
        g.x + reorder_pos.x - reorder_grab.x,
        g.y + reorder_pos.y - reorder_grab.y,
        g.width,
        g.height,
    };

    if (reorder_tr)
    {
        reorder_tr->set_target_box(box);
        reorder_view->damage();
    }

    if (plugin->drag_manager)
    {
        plugin->drag_manager->update_preview(reorder_view, reorder_pos);
    }
}

/* ---- fraction resize via the resize button ---- */

bool lm_output_t::handle_tile_resize()
{
    if (!plugin->is_tiling())
    {
        return false;
    }

    if (output->is_plugin_active(grab_interface.name))
    {
        return false;
    }

    auto focus = wf::toplevel_cast(wf::get_core().get_cursor_focus_view());
    if (!focus || (focus->get_output() != output))
    {
        return false;
    }

    auto node = lm::view_node_t::get_node(focus);
    if (!node)
    {
        return false; /* not tiled: let the resize plugin handle it */
    }

    auto gc = wf::get_core().get_cursor_position().round_down();
    auto og = output->get_layout_geometry();
    auto g  = node->target_geometry();

    /* Half-view strips: the cursor's half of the target view selects the
     * boundary to drag (sway resize semantics). */
    auto band = plugin->edge_divider_band(node,
        wf::point_t{gc.x - og.x, gc.y - og.y},
        std::max(1, g.width / 2), std::max(1, g.height / 2));
    if (!band)
    {
        return false;
    }

    auto size = output->wset()->get_last_output_geometry().value_or(
        lm::default_output_resolution);
    auto vp = output->wset()->get_current_workspace();
    wf::point_t wsetg{
        gc.x - og.x + vp.x * size.width,
        gc.y - og.y + vp.y * size.height};

    int axis_pos = (band->direction == lm::SPLIT_VERTICAL) ?
        wsetg.x : wsetg.y;
    start_divider_drag(*band, axis_pos);
    return divider_drag_active();
}

void lm_output_t::init()
{
    output->add_activator(key_toggle_mode, &on_toggle_mode);
    output->add_activator(key_toggle_float, &on_toggle_float);
    output->add_activator(key_split_horizontal, &on_split_horizontal);
    output->add_activator(key_split_vertical, &on_split_vertical);
    output->add_button(button_tile_move, &on_tile_move);
    output->add_button(button_tile_resize, &on_tile_resize);
    input_grab = std::make_unique<wf::input_grab_t>("layout-manager", output,
        nullptr, nullptr, nullptr);

    output->connect(&on_view_mapped);
    output->connect(&on_tile_request);
    output->connect(&on_fullscreen_request);
    output->connect(&on_view_change_workspace);
    output->connect(&on_view_minimized);
    LOGI("layout-manager: initialized on ", output->to_string());
}

void lm_output_t::fini()
{
    output->rem_binding(&on_toggle_mode);
    output->rem_binding(&on_toggle_float);
    output->rem_binding(&on_split_horizontal);
    output->rem_binding(&on_split_vertical);
    output->rem_binding(&on_tile_move);
    output->rem_binding(&on_tile_resize);
    stop_divider_drag();
    finish_move_drag(true);
}
} // namespace lm

DECLARE_WAYFIRE_PLUGIN(lm::wayfire_layout_manager);
