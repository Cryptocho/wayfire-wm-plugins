/* SPDX-License-Identifier: GPL-2.0-only */
/* output-menu — per-output menu runtime (M3: single-level root menu; M4
 * adds the submenu stack + keyboard; M5 actions; M6 pipemenus). */
#include "output-menu.hpp"
#include "menu-actions.hpp"
#include "menu-dynamic.hpp"

#include <wayfire/scene-operations.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

#include <cairo.h>
#include <pango/pangocairo.h>
#include <linux/input-event-codes.h>
#include <wayfire/workarea.hpp>

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
    {
        /* Forward node damage up the render chain (same as core's
         * default_render_instance_t). Without this, damage_node() on the
         * menu node is a dead letter: the output render pass never sees
         * the region and damage-driven outputs never repaint the menu —
         * hover/keyboard highlight silently never appears (headless
         * repaints eagerly and masked the bug). */
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
    std::shared_ptr<MenuNode> node;
    wf::scene::damage_callback push_damage;
    wf::signal::connection_t<wf::scene::node_damage_signal> on_node_damaged =
        [=] (wf::scene::node_damage_signal *data)
    {
        push_damage(data->region);
    };
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

/* Next selectable item index at/after @from (wrapping; dir is +/-1).
 * Free function taking an explicit Menu* — must not capture a reference into
 * the open stack, which is reallocated when a submenu level is pushed. */
static int next_selectable_in(Menu *menu, int from, int dir)
{
    const int n = (int)menu->items.size();
    if (n == 0)
    {
        return -1;
    }

    int i = from;
    for (int step = 0; step < n; step++)
    {
        i = (i + dir + n) % n;
        if (menu->items[i]->selectable)
        {
            return i;
        }
    }

    return from;
}

uint64_t OutputMenu::now_msec()
{
    return now_msec_impl();
}

OutputMenu::OutputMenu(wf::output_t *output) : output(output), pipe(this)
{
    /* route grabbed events to this: the grab node forwards
     * pointer/touch/keyboard events to the interactions given here;
     * NULL (the old default) silently drops them all */
    this->input_grab = std::make_unique<wf::input_grab_t>(
        "ob-menu", output, this, this, this);
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
    /* rebuild dynamic entries before any Menu* escapes into the stack */
    refresh_dynamic_menus(state.set, output);
    Menu *menu  = state.set.find(id);
    if (!menu)
    {
        LOGE("ob-menu: no such menu '", id, "'");
        return;
    }

    if (stack.empty() && !tree_active)
    {
        this->input_grab->grab_input(wf::scene::layer::OVERLAY);
        opened_by_press = true;
        open_msec       = now_msec_impl();
        wf::scene::add_front(
            output->node_for_layer(wf::scene::layer::TOP), node);
        tree_active = true;
    }

    /* pipemenu: lazy spawn unless its content is already cached
     * (labwc menu_open_root → open_pipemenu_async) */
    if (!menu->execute.empty() && menu->items.empty())
    {
        if (!pipe.spawn_async(menu, nullptr,
            {(int)anchor.x, (int)anchor.y}))
        {
            LOGE("ob-menu: pipemenu spawn failed for '", menu->id, "'");
        }

        /* tree stays active with an empty stack while waiting (labwc
         * sets menu_current + grabs before the data arrives) */
        return;
    }

    OpenLevel level;
    layout_level(level, menu, {(int)anchor.x, (int)anchor.y});
    stack.push_back(std::move(level));
    sync_damage();
}

void OutputMenu::close_all(bool restore)
{
    if (!tree_active)
    {
        return;
    }

    /* cancel a pending pipemenu spawn + drop generated content so the
     * next open re-spawns (labwc _close + reset_pipemenus) */
    pipe.cancel();
    stack.clear();
    tree_active = false;
    if (input_grab->is_grabbed())
    {
        input_grab->ungrab_input();
    }

    wf::scene::remove_child(node);
    sync_damage();
    reset_pipemenus(MenuState::get().set);
    (void)restore;
}

void OutputMenu::sync_damage()
{
    /* the MenuRenderInstance forwards this up the render chain (see its
     * constructor); the explicit frame request covers the case where no
     * other damage would trigger an output repaint */
    wf::scene::damage_node(node, output->get_layout_geometry());
    output->render->schedule_redraw();
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
    if (getenv("OB_MENU_INPUT_DEBUG"))
    {
        LOGI("ob-menu: pointer motion global=", global.x, ",", global.y);
    }

    if (stack.empty())
    {
        return;
    }

    /* labwc menu_process_item_selection: no selection changes while a
     * pipemenu is spawning */
    if (pipe.waiting)
    {
        return;
    }

    /* find the deepest level containing the cursor */
    int hit_level = -1, hit_item = -1;
    for (size_t d = 0; d < stack.size(); d++)
    {
        for (size_t i = 0; i < stack[d].item_rects.size(); i++)
        {
            if ((stack[d].item_rects[i] & wf::point_t{(int)global.x,
                (int)global.y}) && stack[d].menu->items[i]->selectable)
            {
                hit_level = (int)d;
                hit_item  = (int)i;
            }
        }
    }

    if (hit_level < 0)
    {
        return; /* outside all menus; outside-click closes */
    }

    close_to_level(hit_level + 1);
    stack[hit_level].selected = hit_item;
    sync_damage();

    auto *item = stack[hit_level].menu->items[hit_item].get();
    if (item->submenu)
    {
        open_submenu(hit_level, item);
    }
}

void OutputMenu::run_actions(MenuItem *item)
{
    /* whitelist lives in menu-actions.cpp; menu is already closed by
     * every caller, so focus returns to the view first */
    run_menu_actions(output, item, focused_view);
}

void OutputMenu::handle_pointer_button(const wlr_pointer_button_event& ev,
    bool *dismissed)
{
    if (getenv("OB_MENU_INPUT_DEBUG"))
    {
        LOGI("ob-menu: pointer button ", ev.button, " state ", ev.state);
    }

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

    if (pipe.waiting)
    {
        /* selection blocked while a pipemenu spawns (labwc); only an
         * outside-click may dismiss the tree */
        auto cur = output->get_cursor_position();
        for (size_t d = 0; d < stack.size(); d++)
        {
            for (size_t i = 0; i < stack[d].item_rects.size(); i++)
            {
                if (stack[d].item_rects[i] & wf::point_t{(int)cur.x,
                    (int)cur.y})
                {
                    return;
                }
            }
        }

        close_all();
        if (dismissed)
        {
            *dismissed = true;
        }

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
    r["waiting"] = pipe.waiting;
    r["levels"] = (uint64_t)stack.size();
    if (!stack.empty())
    {
        r["menu_id"] = stack[0].menu->id;
        r["items"]   = (uint64_t)stack[0].menu->items.size();
        r["selected"] = stack[0].selected;
        /* root level box — used by input-path tests to locate menu rows */
        wf::json_t rb = wf::json_t::array();
        rb.append(stack[0].box.x);
        rb.append(stack[0].box.y);
        rb.append((uint64_t)stack[0].box.width);
        rb.append((uint64_t)stack[0].box.height);
        r["root_box"] = rb;
        wf::json_t lv = wf::json_t::array();
        for (auto &level : stack)
        {
            wf::json_t j;
            j["menu_id"] = level.menu->id;
            j["items"]   = (uint64_t)level.menu->items.size();
            j["selected"] = level.selected;
            lv.append(j);
        }

        r["stack"] = lv;
    }

    return r;
}

void OutputMenu::handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event event)
{
    if (getenv("OB_MENU_INPUT_DEBUG"))
    {
        LOGI("ob-menu: keyboard key ", event.keycode, " state ", event.state);
    }

    if (event.state != WL_KEYBOARD_KEY_STATE_PRESSED)
    {
        return;
    }

    /* wlr_keyboard_key_event.keycode is already the evdev code (wayfire's
     * own binding matching uses it as-is); the +8 xkb offset only applies
     * to xkb_state_update_key, not here */
    const int ev = (int)event.keycode;
    static const std::pair<int, const char*> keys[] = {
        {KEY_UP, "up"}, {KEY_DOWN, "down"},
        {KEY_LEFT, "left"}, {KEY_RIGHT, "right"},
        {KEY_ENTER, "enter"}, {KEY_KPENTER, "enter"},
        {KEY_ESC, "escape"},
    };
    for (auto &[code, name] : keys)
    {
        if (ev == code)
        {
            handle_nav_key(name);
            return;
        }
    }

    /* accelerators: KEY_A..KEY_Z are contiguous */
    if ((ev >= KEY_A) && (ev <= KEY_Z))
    {
        handle_nav_key(std::string("accel:") +
            (char)('a' + (ev - KEY_A)));
    }
}

void OutputMenu::handle_nav_key(const std::string &key)
{
    if (stack.empty())
    {
        /* tree may still be active with an empty stack (pipemenu spawn
         * pending or failed): only escape is meaningful */
        if ((key == "escape") && tree_active)
        {
            close_all();
        }

        return;
    }

    /* labwc menu_process_item_selection: no selection while waiting */
    if (pipe.waiting && (key != "escape"))
    {
        return;
    }

    auto &level = stack.back();

    if (key == "escape")
    {
        if (stack.size() <= 1)
        {
            close_all();
        } else
        {
            close_to_level(stack.size() - 1);
        }

        sync_damage();
        return;
    }

    if (key == "down")
    {
        level.selected = next_selectable_in(level.menu, level.selected, +1);
        sync_damage();
        return;
    }

    if (key == "up")
    {
        level.selected = next_selectable_in(level.menu, level.selected, -1);
        sync_damage();
        return;
    }

    if ((key == "right") || (key == "enter"))
    {
        if ((level.selected >= 0) &&
            level.menu->items[level.selected]->submenu)
        {
            auto *item = level.menu->items[level.selected].get();
            open_submenu(stack.size() - 1, item);
            /* select first selectable in the child; re-take the reference
             * AFTER open_submenu pushed (the stack vector reallocates) */
            if (stack.size() > 1)
            {
                auto &child = stack.back();
                child.selected = next_selectable_in(child.menu, -1, +1);
            }

            sync_damage();
            return;
        }

        if (key == "enter")
        {
            if ((level.selected >= 0) &&
                (level.menu->items[level.selected]->type == ItemType::Item))
            {
                auto *item = level.menu->items[level.selected].get();
                close_all();
                run_actions(item);
            }
        }

        return;
    }

    if (key == "left")
    {
        if (stack.size() > 1)
        {
            close_to_level(stack.size() - 1);
            sync_damage();
        }

        return;
    }

    if (key.rfind("accel:", 0) == 0)
    {
        char letter = key[6];
        for (size_t i = 0; i < level.menu->items.size(); i++)
        {
            auto &item = level.menu->items[i];
            if ((item->accelerator >= 0) &&
                (item->accelerator + 1 < (int)item->text.size()) &&
                (tolower((unsigned char)item->text[item->accelerator + 1]) ==
                    letter))
            {
                level.selected = (int)i;
                sync_damage();
                if (item->submenu)
                {
                    open_submenu(stack.size() - 1, item.get());
                }

                return;
            }
        }
    }
}

void OutputMenu::open_client_menu(wayfire_toplevel_view view)
{
    if (!view)
    {
        return;
    }

    focused_view = view;
    auto g = view->get_geometry();
    open_root("client-menu", wf::pointf_t{(double)g.x + 24, (double)g.y + 8});
}

void OutputMenu::open_client_menu(wayfire_toplevel_view view,
    wf::pointf_t anchor)
{
    if (!view)
    {
        return;
    }

    focused_view = view;
    open_root("client-menu", anchor);
}

void OutputMenu::open_submenu(size_t level_idx, MenuItem *item)
{
    if (!item->submenu)
    {
        return;
    }

    /* already open as the next level? */
    if ((stack.size() > level_idx + 1) &&
        (stack[level_idx + 1].via_item == item))
    {
        return;
    }

    close_to_level(level_idx + 1);

    /* pipemenu submenu: lazy spawn unless cached (labwc
     * menu_process_item_selection → open_pipemenu_async) */
    if (!item->submenu->execute.empty() && item->submenu->items.empty())
    {
        auto &parent = stack[level_idx];
        const auto &anchor_rect = parent.item_rects[
            parent.selected >= 0 ? parent.selected : 0];
        if (!pipe.spawn_async(item->submenu, item,
            {anchor_rect.x, anchor_rect.y}))
        {
            LOGE("ob-menu: pipemenu spawn failed for '", item->submenu->id,
                "'");
        }

        return;
    }

    auto &parent = stack[level_idx];
    auto &state  = MenuState::get();
    const int ox = ob_theme_menu_overlap_x(state.ob);
    const int oy = ob_theme_menu_overlap_y(state.ob);
    const int b  = ob_theme_menu_border_width(state.ob);

    /* measure child size */
    const int px = ob_theme_menu_items_padding_x(state.ob);
    const int ih = ob_theme_menu_item_height(state.ob);
    int max_w = 0;
    for (auto &it : item->submenu->items)
    {
        max_w = std::max(max_w, measure_text_width(it->text, ""));
    }

    int w = max_w + 2 * px + 2 * b;
    if (item->submenu->width_min > 0)
    {
        w = std::max(w, item->submenu->width_min);
    }

    if (item->submenu->width_max > 0)
    {
        w = std::min(w, item->submenu->width_max);
    }

    int h = 2 * b + (int)item->submenu->items.size() * ih;
    const auto &anchor_rect = parent.item_rects[
        parent.selected >= 0 ? parent.selected : 0];
    const auto &outg = output->get_layout_geometry();

    /* position like labwc: parent align_left -> child opens to the left */
    int x = parent.align_left ?
        (anchor_rect.x - w + ox + b) :
        (anchor_rect.x + anchor_rect.width - ox - b);
    int y = anchor_rect.y - oy;

    /* unconstrain against the workarea (XDG positioner rules) */
    /* own unconstrain (equivalent to the XDG positioner
     * FLIP_X|SLIDE_X|SLIDE_Y adjustments vs the workarea) */
    wlr_box box{x + outg.x, y + outg.y, w, h};
    wlr_box usable = output->workarea->get_workarea();
    if (box.x + (int)box.width > usable.x + usable.width)
    {
        box.x = usable.x + usable.width - box.width;
    }

    if (box.x < usable.x)
    {
        box.x = usable.x;
    }

    if (box.y + (int)box.height > usable.y + usable.height)
    {
        box.y = usable.y + usable.height - box.height;
    }

    if (box.y < usable.y)
    {
        box.y = usable.y;
    }

    OpenLevel child;
    child.menu = item->submenu;
    child.via_item = item;
    layout_level(child, item->submenu, {box.x - outg.x, box.y - outg.y});
    child.align_left = box.x < anchor_rect.x;
    stack.push_back(std::move(child));
    sync_damage();
}

void OutputMenu::close_to_level(size_t depth)
{
    if (stack.size() <= depth)
    {
        return;
    }

    stack.resize(depth);
    sync_damage();
}

void OutputMenu::open_pipe_level(PipeSpawn &sp)
{
    if (sp.via_item == nullptr)
    {
        /* pipemenu opened as root (ctx.anchor is the open anchor) */
        OpenLevel level;
        layout_level(level, sp.pipemenu, sp.anchor);
        stack.push_back(std::move(level));
        sync_damage();
        return;
    }

    /* find the parent level that holds via_item (waiting blocked all
     * selection changes, so it is still the level we spawned from) */
    for (size_t d = stack.size(); d-- > 0;)
    {
        for (auto &it : stack[d].menu->items)
        {
            if (it.get() == sp.via_item)
            {
                open_submenu(d, sp.via_item);
                return;
            }
        }
    }

    LOGE("ob-menu: pipemenu parent level vanished; dropping output");
}

void OutputMenu::activate(MenuItem *item)
{
    close_all();
    run_actions(item);
}
} // namespace obmenu
