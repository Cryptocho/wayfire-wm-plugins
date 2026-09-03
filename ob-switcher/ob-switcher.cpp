/* SPDX-License-Identifier: GPL-2.0-only */
/* ob-switcher — window switcher: wayfire's fast-switcher behavior (copied
 * from plugins/single_plugins/fast-switcher.cpp) plus a labwc-cycle-style
 * OSD themed by the openbox themerc osd.* / window_switcher.* keys through
 * libobtheme. The theme pointer is re-read on every render, so ob-theme
 * in-place reloads are picked up automatically; without any theme the OSD
 * falls back to built-in colors.
 *
 * Differences vs upstream fast-switcher (intentional):
 *   - activator bindings instead of add_key (ob-menu lesson: alt-modified
 *     keybinding matching silently failed in this environment);
 *   - Esc cancels without changing focus (labwc semantics);
 *   - clicking an OSD row selects that window (labwc semantics);
 *   - core-level plugin class for per-output tracking + debug IPC.
 * Differences vs labwc cycle (see TEST.md): no live preview-node
 * reparenting, minimized windows excluded, one OSD on the activating
 * output only, OSD list truncated at MAX_OSD_ROWS (no scrollbar yet).
 */
#include <wayfire/core.hpp>
#include <wayfire/plugins/common/input-grab.hpp>
#include <wayfire/plugins/common/util.hpp>
#include <wayfire/scene-input.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

#include <obtheme.h>
#include <pango/pangocairo.h>
#include <cairo.h>
#include <linux/input-event-codes.h>

#include "../ob-theme/ob-theme-data.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace obswitcher
{
/* Built-in fallback look for sessions where no theme was initialized. */
static constexpr float FB_OSD_BG[4]     = {0.10f, 0.10f, 0.12f, 0.95f};
static constexpr float FB_OSD_BORDER[4] = {0.35f, 0.55f, 0.85f, 1.0f};
static constexpr float FB_OSD_TEXT[4]   = {0.92f, 0.92f, 0.92f, 1.0f};
static constexpr float FB_ACTIVE_BG[4]  = {0.22f, 0.38f, 0.62f, 1.0f};
static constexpr float FB_ACTIVE_BORDER[4] = {0.45f, 0.65f, 0.95f, 1.0f};
/* max OSD rows; beyond that the list is truncated (no scrollbar yet) */
static constexpr int MAX_OSD_ROWS = 12;

/* Premultiplied theme color -> straight alpha for cairo. */
static void cairo_set_theme_color(cairo_t *cr, const float *c)
{
    if (c[3] > 0.0f)
    {
        cairo_set_source_rgba(cr, c[0] / c[3], c[1] / c[3],
            c[2] / c[3], c[3]);
    } else
    {
        cairo_set_source_rgba(cr, 0, 0, 0, 0);
    }
}

/* Pango-render one line of OSD text (left-aligned, ellipsized). */
static cairo_surface_t *render_osd_text(const std::string& text, int width,
    int height, const float *color, const std::string& font_string)
{
    auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
        std::max(1, width), std::max(1, height));
    if ((width <= 0) || (height <= 0))
    {
        return surface;
    }

    auto cr = cairo_create(surface);
    cairo_set_theme_color(cr, color);
    PangoFontDescription *desc =
        pango_font_description_from_string(font_string.c_str());
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, text.c_str(), (int)text.size());
    pango_layout_set_width(layout, width * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(desc);
    g_object_unref(layout);
    cairo_destroy(cr);
    return surface;
}

/* OSD font: ob-theme/font_osd → ob-theme/font → "sans 10". */
static std::string osd_font_string()
{
    try
    {
        wf::option_wrapper_t<std::string> osd{"ob-theme/font_osd"};
        if (!((std::string)osd).empty())
        {
            return osd.value();
        }

        wf::option_wrapper_t<std::string> base{"ob-theme/font"};
        if (!((std::string)base).empty())
        {
            return base.value();
        }
    } catch (const std::exception&)
    {
        /* ob-theme metadata absent */
    }

    return "sans 10";
}

/**
 * One output's switcher OSD. Self-contained: rows + selection are pushed
 * in via update(), rendering reads only its own state + the live theme.
 */
class SwitcherOSD
{
  public:
    struct Row
    {
        std::string app_id;
        std::string title;
    };

    class OsdNode final : public wf::scene::node_t
    {
      public:
        explicit OsdNode(SwitcherOSD *owner) : node_t(true), owner(owner)
        {}

        void gen_render_instances(
            std::vector<wf::scene::render_instance_uptr>& instances,
            wf::scene::damage_callback push_damage,
            wf::output_t *output = nullptr) override
        {
            instances.push_back(
                std::make_unique<OsdRenderInstance>(
                    std::dynamic_pointer_cast<OsdNode>(shared_from_this()),
                    push_damage));
        }

        wf::geometry_t get_bounding_box() override
        {
            return owner->osd_box;
        }

        SwitcherOSD *owner;
    };

    class OsdRenderInstance final : public wf::scene::render_instance_t
    {
      public:
        OsdRenderInstance(std::shared_ptr<OsdNode> node,
            wf::scene::damage_callback push_damage) : node(node),
            push_damage(push_damage)
        {
            /* Forward node damage up the render chain (ob-menu lesson:
             * without this, damage_node() on the OSD is a dead letter and
             * the highlight never repaints on damage-driven outputs). */
            node->connect(&on_node_damaged);
        }

        void schedule_instructions(
            std::vector<wf::scene::render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::region_t& damage) override
        {
            auto full = node->owner->output->get_layout_geometry();
            wf::region_t our_damage = damage & full;
            if (!our_damage.empty())
            {
                instructions.push_back(wf::scene::render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = std::move(our_damage),
                    });
            }
        }

        void render(const wf::scene::render_instruction_t& data) override
        {
            node->owner->render(data);
        }

        void presentation_feedback(wf::output_t*) override
        {}

      private:
        std::shared_ptr<OsdNode> node;
        wf::scene::damage_callback push_damage;
        wf::signal::connection_t<wf::scene::node_damage_signal>
        on_node_damaged;
    };

    explicit SwitcherOSD(wf::output_t *output) : output(output)
    {
        node = std::make_shared<OsdNode>(this);
        wf::scene::add_front(
            output->node_for_layer(wf::scene::layer::OVERLAY), node);
    }

    ~SwitcherOSD()
    {
        wf::scene::remove_child(node);
    }

    /* Push rows + selection, recompute geometry, damage old+new extents. */
    void update(const std::vector<Row>& new_rows, size_t new_selected)
    {
        wf::geometry_t old = osd_box;
        rows     = new_rows;
        selected = new_selected;
        row_boxes.clear();

        auto out_geo = output->get_layout_geometry();
        struct theme *t = ob_theme_current();

        const int bw  = t ? ob_theme_osd_border_width(t) : 1;
        const int pad = (t ? ob_theme_switcher_padding(t) : 6) + bw;
        int width     = t ? ob_theme_switcher_width(t) : 500;
        if (t && ob_theme_switcher_width_is_percent(t))
        {
            width = out_geo.width * width / 100;
        }

        const int item_h  = item_height(t);
        const int nr_rows = std::min((int)rows.size(), MAX_OSD_ROWS);
        const int height  = 2 * pad + nr_rows * item_h;

        osd_box = wf::geometry_t{
            out_geo.x + (out_geo.width - width) / 2,
            out_geo.y + (out_geo.height - height) / 2,
            (uint32_t)std::max(1, width), (uint32_t)std::max(1, height),
        };

        const int sel_bw = active_border_width(t);
        for (int i = 0; i < nr_rows; i++)
        {
            row_boxes.push_back(wf::geometry_t{
                    osd_box.x + pad + sel_bw,
                    osd_box.y + pad + i * item_h,
                    (uint32_t)std::max(1,
                        width - 2 * (pad + sel_bw)),
                    (uint32_t)item_h,
                });
        }

        /* damage old + new so the move and the content change both repaint */
        wf::scene::damage_node(node, old);
        wf::scene::damage_node(node, osd_box);
    }

    /* Hit test (output-layout/global coordinates); -1 outside any row. */
    int row_at(wf::pointf_t global) const
    {
        wf::point_t p{(int)global.x, (int)global.y};
        for (size_t i = 0; i < row_boxes.size(); i++)
        {
            if (row_boxes[i] & p)
            {
                return (int)i;
            }
        }

        return -1;
    }

    void render(const wf::scene::render_instruction_t& data)
    {
        struct theme *t = ob_theme_current();
        const float *bg     = t ? ob_theme_osd_bg(t) : FB_OSD_BG;
        const float *border = t ? ob_theme_osd_border_color(t) : FB_OSD_BORDER;
        const float *text_c = t ? ob_theme_osd_label_text_color(t) : FB_OSD_TEXT;
        const int bw     = t ? ob_theme_osd_border_width(t) : 1;
        const int sel_bw = active_border_width(t);
        const int px     = t ? ob_theme_switcher_item_padding_x(t) : 8;
        const int icon   = t ? ob_theme_switcher_item_icon_size(t) : 24;
        const std::string font = osd_font_string();
        const wf::color_t bg_c{bg[0], bg[1], bg[2], bg[3]};

        /* backdrop: border color fills the box, bg is inset by the border
         * (same paint order as labwc / ob-menu) */
        data.pass->add_rect(wf::color_t{border[0], border[1], border[2],
            border[3]}, data.target, osd_box, data.damage);
        wf::geometry_t inner{
            osd_box.x + bw, osd_box.y + bw,
            (uint32_t)std::max(0, (int)osd_box.width - 2 * bw),
            (uint32_t)std::max(0, (int)osd_box.height - 2 * bw),
        };
        data.pass->add_rect(bg_c, data.target, inner, data.damage);

        for (size_t i = 0; i < row_boxes.size(); i++)
        {
            auto& box  = row_boxes[i];
            const bool sel = ((int)i == (int)selected);

            if (sel)
            {
                /* labwc classic selection: active border, then active bg
                 * inset by the border width */
                const float *sb = t ?
                    ob_theme_switcher_item_active_border_color(t) :
                    FB_ACTIVE_BORDER;
                const float *sg = t ?
                    ob_theme_switcher_item_active_bg(t) : FB_ACTIVE_BG;
                data.pass->add_rect(wf::color_t{sb[0], sb[1], sb[2], sb[3]},
                    data.target, box, data.damage);
                wf::geometry_t row_inner{
                    box.x + sel_bw, box.y + sel_bw,
                    (uint32_t)std::max(0, (int)box.width - 2 * sel_bw),
                    (uint32_t)std::max(0, (int)box.height - 2 * sel_bw),
                };
                data.pass->add_rect(wf::color_t{sg[0], sg[1], sg[2], sg[3]},
                    data.target, row_inner, data.damage);
            }

            const std::string &app_id = rows[i].app_id;
            const std::string &title  = rows[i].title;

            /* icon placeholder: translucent square with the app_id initial */
            int tx = box.x + px;
            if ((icon > 0) && !app_id.empty())
            {
                int ib = box.y + ((int)box.height - icon) / 2;
                wf::geometry_t icon_box{box.x + px, ib,
                    (uint32_t)icon, (uint32_t)icon};
                const float *ibg = t ?
                    ob_theme_switcher_item_active_bg(t) : FB_ACTIVE_BG;
                data.pass->add_rect(
                    wf::color_t{ibg[0], ibg[1], ibg[2], 0.6f * ibg[3]},
                    data.target, icon_box, data.damage);

                auto surf = render_osd_text(app_id.substr(0, 1), icon, icon,
                    text_c, font);
                wf::owned_texture_t tex{surf};
                cairo_surface_destroy(surf);
                data.pass->add_texture(tex.get_texture(), data.target,
                    icon_box, data.damage);
            }

            /* two text columns: app_id (fixed 180px) + title (the rest) */
            tx = box.x + px + icon + px;
            const int id_w = 180;
            if ((tx + id_w) < (box.x + (int)box.width))
            {
                auto surf_id = render_osd_text(app_id, id_w, box.height,
                    text_c, font);
                wf::owned_texture_t tex_id{surf_id};
                cairo_surface_destroy(surf_id);
                data.pass->add_texture(tex_id.get_texture(), data.target,
                    wf::geometry_t{tx, box.y, (uint32_t)id_w,
                        (uint32_t)box.height}, data.damage);

                int title_x = tx + id_w + px;
                int title_w = (box.x + (int)box.width - px) - title_x;
                if (title_w > 0)
                {
                    auto surf_t = render_osd_text(title, title_w, box.height,
                        text_c, font);
                    wf::owned_texture_t tex_t{surf_t};
                    cairo_surface_destroy(surf_t);
                    data.pass->add_texture(tex_t.get_texture(), data.target,
                        wf::geometry_t{title_x, box.y, (uint32_t)title_w,
                            (uint32_t)box.height}, data.damage);
                }
            }
        }
    }

    wf::geometry_t osd_box = {0, 0, 0, 0};
    std::vector<wf::geometry_t> row_boxes;

    wf::output_t *output;
    std::shared_ptr<OsdNode> node;

  private:
    /* OSD content snapshot, pushed by refresh_osd() on every change. */
    std::vector<Row> rows;
    size_t selected = 0;

    static int active_border_width(struct theme *t)
    {
        return t ? ob_theme_switcher_item_active_border_width(t) : 2;
    }

    static int item_height(struct theme *t)
    {
        /* labwc derives classic item_height from the tallest titlebar
         * object; vendored post_processing computes it. */
        return t ? t->osd_window_switcher_classic.item_height : 32;
    }
};

/* ------------------------------------------------------------------ */
/* Per-output switcher instance: fast-switcher logic + OSD.            */
/* ------------------------------------------------------------------ */
class ob_switcher_output_t : public wf::per_output_plugin_instance_t,
    public wf::keyboard_interaction_t, public wf::pointer_interaction_t
{
  public:
    void init() override
    {
        output->add_activator(activate_key, &fast_switch);
        output->add_activator(activate_backward_key, &fast_switch_backward);
        input_grab = std::make_unique<wf::input_grab_t>("ob-switcher",
            output, this, this, nullptr);
        grab_interface.cancel = [=] ()
        {
            switch_terminate(false);
        };
    }

    void fini() override
    {
        if (active)
        {
            switch_terminate(false);
        }

        output->rem_binding(&fast_switch);
        output->rem_binding(&fast_switch_backward);
    }

    void handle_keyboard_key(wf::seat_t*,
        wlr_keyboard_key_event event) override
    {
        auto mod = wf::get_core().seat->modifier_from_keycode(event.keycode);
        if ((event.state == WLR_KEY_PRESSED) &&
            (event.keycode == KEY_ESC))
        {
            /* labwc: Esc deactivates the switcher without changing focus */
            switch_terminate(false);
            return;
        }

        if ((event.state == WLR_KEY_RELEASED) &&
            (mod & activating_modifiers))
        {
            switch_terminate(true);
        }
    }

    /* pointer_interaction_t: click an OSD row to select it (labwc). */
    void handle_pointer_button(const wlr_pointer_button_event& ev) override
    {
        if (ev.state != WLR_BUTTON_RELEASED)
        {
            return;
        }

        int row = osd ? osd->row_at(wf::get_core().get_cursor_position()) : -1;
        if ((row >= 0) && (row < (int)views.size()))
        {
            current_view_index = row;
            switch_terminate(true);
        }
    }

    void handle_pointer_motion(wf::pointf_t, uint32_t) override
    {}

    /* ---- IPC hooks (headless assertions) ---- */
    bool ipc_activate(bool forward)
    {
        return do_switch(forward);
    }

    void ipc_cancel()
    {
        switch_terminate(false);
    }

    wf::json_t ipc_state() const
    {
        wf::json_t r;
        r["active"]  = active;
        r["nr_views"] = (int)views.size();
        r["current_index"] = (int)current_view_index;
        r["osd_visible"] = osd != nullptr;
        return r;
    }

  private:
    wf::option_wrapper_t<wf::activatorbinding_t> activate_key{
        "ob-switcher/activate"};
    wf::option_wrapper_t<wf::activatorbinding_t> activate_backward_key{
        "ob-switcher/activate_backward"};
    wf::option_wrapper_t<double> inactive_alpha{
        "ob-switcher/inactive_alpha"};
    wf::option_wrapper_t<bool> osd_show{"ob-switcher/osd_show"};

    std::vector<wayfire_toplevel_view> views;
    size_t current_view_index = 0;
    uint32_t activating_modifiers = 0;
    bool active = false;
    std::unique_ptr<wf::input_grab_t> input_grab;
    std::unique_ptr<SwitcherOSD> osd;
    wayfire_toplevel_view originally_active; // restored on cancel

    wf::plugin_activation_data_t grab_interface = {
        .name = "ob-switcher",
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
    };

    void view_chosen(int i, bool reorder_only)
    {
        if (!((0 <= i) && (i < (int)views.size())))
        {
            return;
        }

        current_view_index = i;
        set_view_highlighted(views[i], true);

        for (int j = (int)views.size() - 1; j >= 0; j--)
        {
            wf::view_bring_to_front(views[j]);
        }

        if (reorder_only)
        {
            wf::view_bring_to_front(views[i]);
        } else
        {
            wf::get_core().default_wm->focus_raise_view(views[i]);
        }
    }

    wf::signal::connection_t<wf::view_disappeared_signal> cleanup_view =
        [=] (wf::view_disappeared_signal *ev)
    {
        size_t i = 0;
        for (; i < views.size() && views[i] != ev->view; i++)
        {}

        if (i == views.size())
        {
            return;
        }

        views.erase(views.begin() + i);
        if (views.empty())
        {
            switch_terminate(false);
            return;
        }

        if (i <= current_view_index)
        {
            int new_index =
                (current_view_index + views.size() - 1) % views.size();
            view_chosen(new_index, true);
        }
        else
        {
            refresh_osd();
        }
    };

    const std::string transformer_name = "ob-switcher";

    void set_view_alpha(wayfire_toplevel_view view, float alpha)
    {
        auto tr = wf::ensure_named_transformer<wf::scene::view_2d_transformer_t>(
            view, wf::TRANSFORMER_2D, transformer_name, view);
        view->get_transformed_node()->begin_transform_update();
        tr->alpha = alpha;
        view->get_transformed_node()->end_transform_update();
    }

    void set_view_highlighted(wayfire_toplevel_view view, bool selected)
    {
        view->set_activated(selected); // changes decoration focus state
        double alpha = selected ? 1.0 : inactive_alpha.value();
        set_view_alpha(view, alpha);
    }

    void update_views()
    {
        views = output->wset()->get_views(
            wf::WSET_CURRENT_WORKSPACE | wf::WSET_MAPPED_ONLY |
            wf::WSET_EXCLUDE_MINIMIZED);
        std::sort(views.begin(), views.end(),
            [] (wayfire_toplevel_view& a, wayfire_toplevel_view& b)
        {
            return wf::get_focus_timestamp(a) > wf::get_focus_timestamp(b);
        });
    }

    bool do_switch(bool forward)
    {
        if (active)
        {
            switch_next(forward);
            return true;
        }

        if (!output->activate_plugin(&grab_interface))
        {
            return false;
        }

        update_views();

        if (views.size() < 1)
        {
            output->deactivate_plugin(&grab_interface);
            return false;
        }

        originally_active = wf::toplevel_cast(
            wf::get_core().seat->get_active_view());
        current_view_index = 0;
        active = true;

        for (auto view : views)
        {
            set_view_highlighted(view, false);
        }

        if (osd_show)
        {
            osd = std::make_unique<SwitcherOSD>(output);
            refresh_osd();
        }

        input_grab->grab_input(wf::scene::layer::OVERLAY);
        activating_modifiers = wf::get_core().seat->get_keyboard_modifiers();
        switch_next(forward);

        output->connect(&cleanup_view);
        return true;
    }

    wf::activator_callback fast_switch = [=] (auto)
    {
        return do_switch(true);
    };

    wf::activator_callback fast_switch_backward = [=] (auto)
    {
        return do_switch(false);
    };

    void switch_terminate(bool switch_focus)
    {
        if (!active)
        {
            return;
        }

        if (switch_focus)
        {
            view_chosen(current_view_index, false);
        } else if (originally_active)
        {
            /* cancel: restore the focus-highlight of the view that was
             * active when the switcher opened */
            set_view_highlighted(originally_active, true);
        }

        osd.reset();
        input_grab->ungrab_input();
        output->deactivate_plugin(&grab_interface);

        for (auto view : views)
        {
            view->get_transformed_node()->rem_transformer(transformer_name);
        }

        active = false;
        cleanup_view.disconnect();
        originally_active = nullptr;
    }

    void switch_next(bool forward)
    {
        set_view_highlighted(views[current_view_index], false);

        int index = current_view_index;
        if (forward)
        {
            index = (index + 1) % views.size();
        } else
        {
            index = index ? index - 1 : views.size() - 1;
        }

        view_chosen(index, true);
        refresh_osd();
    }

    void refresh_osd()
    {
        if (!osd)
        {
            return;
        }

        std::vector<SwitcherOSD::Row> rows;
        rows.reserve(views.size());
        for (auto& view : views)
        {
            rows.push_back(SwitcherOSD::Row{
                    view ? view->get_app_id() : "",
                    view ? view->get_title() : "",
                });
        }

        osd->update(rows, current_view_index);
    }
};

/* ------------------------------------------------------------------ */
/* Core-level plugin: per-output tracking + debug IPC.                 */
/* ------------------------------------------------------------------ */
class wayfire_ob_switcher : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        outputs.init_output_tracking();
        register_ipc();
    }

    void fini() override
    {
        ipc_repo->unregister_method("ob-switcher/debug");
        outputs.fini_output_tracking();
    }

  private:
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;

    struct outputs_t : public wf::per_output_tracker_mixin_t<
            ob_switcher_output_t>
    {
        ob_switcher_output_t *get(wf::output_t *output)
        {
            auto it = output_instance.find(output);
            return it == output_instance.end() ? nullptr : it->second.get();
        }
    };

    outputs_t outputs;

    void register_ipc()
    {
        ipc_repo->register_method("ob-switcher/debug",
            [this] (wf::json_t data) -> wf::json_t
        {
            std::string op = data.has_member("op") ?
                (std::string)data["op"] : "get-state";
            auto output = wf::get_core().seat->get_active_output();
            auto *inst  = outputs.get(output);
            wf::json_t r;

            if (!inst)
            {
                r["state"] = nullptr;
                return r;
            }

            if (op == "activate")
            {
                bool forward = !data.has_member("forward") ||
                    (bool)data["forward"];
                r["activated"] = inst->ipc_activate(forward);
            } else if (op == "cancel")
            {
                inst->ipc_cancel();
            }

            r["state"] = inst->ipc_state();
            return r;
        });
    }
};
} // namespace obswitcher

DECLARE_WAYFIRE_PLUGIN(obswitcher::wayfire_ob_switcher);
