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

#include <algorithm>
#include <string>

#include "lm-tree.hpp"
#include "lm-wset.hpp"
#include "lm-float.hpp"
#include "lm-ipc.hpp"

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
    void handle_view_mapped(wayfire_toplevel_view toplevel);

  private:
    wf::option_wrapper_t<wf::activatorbinding_t> key_toggle_mode{
        "layout-manager/key_toggle_mode"};
    wf::option_wrapper_t<wf::activatorbinding_t> key_toggle_float{
        "layout-manager/key_toggle_float"};
    wf::activator_callback on_toggle_mode = [=] (auto)
    {
        return handle_toggle_mode();
    };
    wf::activator_callback on_toggle_float = [=] (auto)
    {
        return handle_toggle_float();
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
    void init() override
    {
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

            {
                lm::autocommit_transaction_t tx;
                for (auto v : views)
                {
                    auto &st = lm_view_state_t::get(v);
                    v->toplevel()->pending().tiled_edges = 0;
                    if (st.has_pre)
                    {
                        v->toplevel()->pending().geometry = st.pre_tiling;
                    }

                    tx.tx->add_object(v->toplevel());
                }
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

    /* ---- outputs & IPC ---- */

    struct outputs_t : public wf::per_output_tracker_mixin_t<lm_output_t>
    {
        wayfire_layout_manager *plugin = nullptr;

        lm_output_t *get(wf::output_t *output)
        {
            auto it = output_instance.find(output);
            return it == output_instance.end() ? nullptr : it->second.get();
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

bool lm_output_t::handle_toggle_float()
{
    auto view = wf::toplevel_cast(wf::get_core().seat->get_active_view());
    if (!view || (view->get_output() != output))
    {
        return false;
    }

    return plugin->toggle_float_view(view);
}

void lm_output_t::init()
{
    output->add_activator(key_toggle_mode, &on_toggle_mode);
    output->add_activator(key_toggle_float, &on_toggle_float);

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
}
} // namespace lm

DECLARE_WAYFIRE_PLUGIN(lm::wayfire_layout_manager);
