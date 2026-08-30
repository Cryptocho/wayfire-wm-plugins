/* SPDX-License-Identifier: GPL-2.0-only */
/* output-menu — per-output menu runtime (M3: single-level root menu; M4
 * adds the submenu stack + keyboard; M5 actions; M6 pipemenus). */
#include "output-menu.hpp"

#include <wayfire/scene-operations.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

#include <cairo.h>
#include <pango/pangocairo.h>
#include <linux/input-event-codes.h>

#include <algorithm>
#include <map>

namespace obmenu
{
/* ------------------------------------------------------------------ */
/* MenuNode: renders the whole open stack, back to front.              */
/* ------------------------------------------------------------------ */
class OutputMenu::MenuNode final : public wf::scene::node_t
{
  public:
    explicit MenuNode(OutputMenu *owner) : node_t(false), owner(owner)
    {}

    void gen_render_instances(
        std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage,
        wf::output_t *output = nullptr) override;

    wf::geometry_t get_bounding_box() override
    {
        return owner->output->get_layout_geometry();
    }

    OutputMenu *owner;
};

class OutputMenu::MenuRenderInstance final : public wf::scene::render_instance_t
{
  public:
    MenuRenderInstance(std::shared_ptr<MenuNode> node,
        wf::scene::damage_callback push_damage) : node(node),
        push_damage(push_damage)
    {}

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
    std::shared_ptr<MenuNode> node;
    wf::scene::damage_callback push_damage;
};

void OutputMenu::MenuNode::gen_render_instances(
    std::vector<wf::scene::render_instance_uptr>& instances,
    wf::scene::damage_callback push_damage, wf::output_t *output)
{
    instances.push_back(
        std::make_unique<MenuRenderInstance>(
            std::dynamic_pointer_cast<MenuNode>(shared_from_this()),
            push_damage));
}

/* ------------------------------------------------------------------ */

static cairo_surface_t *render_menu_text(const std::string &text, int width,
    int height, const std::string &font, const float *color,
    enum lab_justification justify)
{
    auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width,
        height);
    if ((width <= 0) || (height <= 0))
    {
        return surface;
    }

    auto cr = cairo_create(surface);
    PangoFontDescription *desc =
        pango_font_description_from_string(font.c_str());
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_set_width(layout, width * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    if (justify == LAB_JUSTIFY_CENTER)
    {
        pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    } else if (justify == LAB_JUSTIFY_RIGHT)
    {
        pango_layout_set_alignment(layout, PANGO_ALIGN_RIGHT);
    }

    cairo_set_source_rgba(cr, color[0], color[1], color[2], color[3]);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(desc);
    g_object_unref(layout);
    cairo_destroy(cr);
    return surface;
}

static int measure_text_width(const std::string &text, const std::string &font)
{
    auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    auto cr = cairo_create(surface);
    PangoFontDescription *desc =
        pango_font_description_from_string(font.c_str());
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, text.c_str(), -1);
    PangoRectangle rect = {};
    pango_layout_get_extents(layout, NULL, &rect);
    pango_extents_to_pixels(&rect, NULL);
    int width = rect.width;
    pango_font_description_free(desc);
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return width;
}

static uint64_t now_msec_impl()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

uint64_t OutputMenu::now_msec()
{
    return now_msec_impl();
}

OutputMenu::OutputMenu(wf::output_t *output) : output(output)
{
    /* TEMP: no interactions while bisecting the grab crash */
    this->input_grab = std::make_unique<wf::input_grab_t>("ob-menu", output);
    this->node = std::make_shared<MenuNode>(this);
}

OutputMenu::~OutputMenu()
{
    close_all(false);
}

void OutputMenu::layout_level(OpenLevel &level, Menu *menu, wf::point_t pos)
{
    auto &theme = MenuState::get();
    const int b  = ob_theme_menu_border_width(theme.ob);
    const int px = ob_theme_menu_items_padding_x(theme.ob);
    const int ih = ob_theme_menu_item_height(theme.ob);

    int max_w = 0;
    for (auto &item : menu->items)
    {
        int tw = measure_text_width(item->text,
            /* menu item font == ob-decor font in M3 */ "");
        max_w = std::max(max_w, tw);
    }

    int w = max_w + 2 * px + 2 * b;
    if (menu->width_min > 0)
    {
        w = std::max(w, menu->width_min);
    }

    if (menu->width_max > 0)
    {
        w = std::min(w, menu->width_max);
    }

    int h = 2 * b + (int)menu->items.size() * ih;
    level.menu = menu;
    level.box  = {pos.x, pos.y, (uint32_t)w, (uint32_t)h};
    level.item_rects.clear();
    for (size_t i = 0; i < menu->items.size(); i++)
    {
        level.item_rects.push_back({pos.x + b, pos.y + b + (int)i * ih,
            (uint32_t)w, (uint32_t)ih});
    }

    level.selected = -1;
}

void OutputMenu::open_root(const std::string &id, wf::pointf_t anchor)
{
    auto &state = MenuState::get();
    Menu *menu  = state.set.find(id);
    if (!menu)
    {
        LOGE("ob-menu: no such menu '", id, "'");
        return;
    }

    if (stack.empty())
    {
        LOGE("DBG open: grabbing");
        this->input_grab->grab_input(wf::scene::layer::OVERLAY);
        opened_by_press = true;
        open_msec       = now_msec_impl();
        LOGE("DBG open: mounting node");
        wf::scene::add_front(
            output->node_for_layer(wf::scene::layer::TOP), node);
        LOGE("DBG open: mounted");
    }

    OpenLevel level;
    LOGE("DBG open: layout");
    layout_level(level, menu, {(int)anchor.x, (int)anchor.y});
    stack.push_back(std::move(level));
    LOGE("DBG open: damage");
    sync_damage();
    LOGE("DBG open: done");
}

void OutputMenu::close_all(bool restore)
{
    if (stack.empty())
    {
        return;
    }

    stack.clear();
    if (input_grab->is_grabbed())
    {
        input_grab->ungrab_input();
    }

    wf::scene::remove_child(node);
    sync_damage();
    (void)restore;
}

void OutputMenu::sync_damage()
{
    wf::scene::damage_node(node, output->get_layout_geometry());
}

MenuItem *OutputMenu::hovered_item(wf::pointf_t global, size_t *depth)
{
    for (size_t d = stack.size(); d-- > 0;)
    {
        auto &level = stack[d];
        for (size_t i = 0; i < level.item_rects.size(); i++)
        {
            if (level.item_rects[i] & wf::point_t{(int)global.x,
                (int)global.y})
            {
                if (depth)
                {
                    *depth = d;
                }

                return level.menu->items[i].get();
            }
        }
    }

    return nullptr;
}

void OutputMenu::handle_pointer_motion(wf::pointf_t global, uint32_t)
{
    handle_pointer_motion_impl(global);
}

void OutputMenu::render(const wf::scene::render_instruction_t& data)
{
    if (stack.empty())
    {
        return;
    }

    auto &state = MenuState::get();
    const int b = ob_theme_menu_border_width(state.ob);
    const int px = ob_theme_menu_items_padding_x(state.ob);
    const std::string font = "sans 10"; /* M3: ob-decor font passthrough */

    for (auto &level : stack)
    {
        /* border-colored backdrop for the whole menu */
        const float *bc = ob_theme_menu_border_color(state.ob);
        data.pass->add_rect(wf::color_t(bc[0], bc[1], bc[2], bc[3]),
            data.target, level.box, data.damage);

        for (size_t i = 0; i < level.menu->items.size(); i++)
        {
            auto &item  = level.menu->items[i];
            auto &irect = level.item_rects[i];
            if ((item->type == ItemType::Separator) || item->text.empty())
            {
                continue;
            }

            const bool sel = ((int)i == level.selected);
            const float *bg  = sel ? ob_theme_menu_items_active_bg(
                state.ob) : ob_theme_menu_items_bg(state.ob);
            const float *fg  = sel ? ob_theme_menu_items_active_text(
                state.ob) : ob_theme_menu_items_text(state.ob);

            /* item content rect (inside the border) */
            wf::geometry_t content{irect.x + b, irect.y, irect.width - 2 * b,
                irect.height};
            data.pass->add_rect(wf::color_t(bg[0], bg[1], bg[2], bg[3]),
                data.target, content, data.damage);

            /* text (inset by horizontal padding) */
            wf::geometry_t text_rect{content.x + px, content.y,
                content.width - 2 * px, content.height};
            auto surface = render_menu_text(item->text, text_rect.width,
                text_rect.height, font, fg, LAB_JUSTIFY_LEFT);
            wf::owned_texture_t tex{surface};
            cairo_surface_destroy(surface);
            data.pass->add_texture(tex.get_texture(), data.target, text_rect,
                data.damage);
        }
    }
}

void OutputMenu::handle_pointer_motion_impl(wf::pointf_t global)
{
    if (stack.empty())
    {
        return;
    }

    for (size_t d = 0; d < stack.size(); d++)
    {
        auto &level = stack[d];
        int new_sel = -1;
        for (size_t i = 0; i < level.item_rects.size(); i++)
        {
            if ((level.item_rects[i] & wf::point_t{(int)global.x,
                (int)global.y}) && level.menu->items[i]->selectable)
            {
                new_sel = (int)i;
                break;
            }
        }

        if (new_sel != level.selected)
        {
            level.selected = new_sel;
            sync_damage();
        }
    }
}

void OutputMenu::run_actions(MenuItem *item)
{
    /* M3: Execute only; full action table lands in M5. */
    for (auto &action : item->actions)
    {
        if (action.name == "Execute")
        {
            auto cmd = action.attrs["command"];
            if (!cmd.empty())
            {
                wf::get_core().run(cmd);
            }
        }
        else
        {
            LOGE("ob-menu: action '", action.name,
                "' not wired yet (M5)");
        }
    }
}

void OutputMenu::handle_pointer_button(const wlr_pointer_button_event& ev,
    bool *dismissed)
{
    if (stack.empty() || (ev.button != BTN_LEFT))
    {
        return;
    }

    auto cursor = output->get_cursor_position();
    if (ev.state == WL_POINTER_BUTTON_STATE_PRESSED)
    {
        (void)cursor;
        return;
    }

    /* release */
    uint64_t held = now_msec_impl() - open_msec;
    if (opened_by_press && (held < 250))
    {
        /* swallow the release that opened the menu (labwc release-guard) */
        return;
    }

    for (size_t d = 0; d < stack.size(); d++)
    {
        auto &level = stack[d];
        for (size_t i = 0; i < level.item_rects.size(); i++)
        {
            if ((level.item_rects[i] & wf::point_t{(int)cursor.x,
                (int)cursor.y}) &&
                (level.menu->items[i]->type == ItemType::Item))
            {
                auto item = level.menu->items[i].get();
                close_all();
                run_actions(item);
                return;
            }
        }
    }

    /* click outside any item: close the tree */
    close_all();
    if (dismissed)
    {
        *dismissed = true;
    }
}

wf::json_t OutputMenu::get_state()
{
    wf::json_t r;
    r["open"] = is_open();
    r["levels"] = (uint64_t)stack.size();
    if (!stack.empty())
    {
        r["menu_id"] = stack[0].menu->id;
        r["items"]   = (uint64_t)stack[0].menu->items.size();
        r["selected"] = stack[0].selected;
    }

    return r;
}

void OutputMenu::handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event)
{
    /* M4 */
}

void OutputMenu::open_submenu(OpenLevel&, MenuItem*)
{
    /* M4 */
}

void OutputMenu::close_to_level(size_t)
{
    /* M4 */
}

void OutputMenu::activate(MenuItem *item)
{
    close_all();
    run_actions(item);
}
} // namespace obmenu
