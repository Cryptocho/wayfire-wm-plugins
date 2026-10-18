#include "deco-theme.hpp"
#include <wayfire/opengl.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/core.hpp>
#include <pango/pangocairo.h>
#include <algorithm>
#include <map>

namespace wf
{
namespace odecor
{
decoration_theme_t::decoration_theme_t()
{
    /* Theme source order: ob-theme provider (owns the singleton) →
     * ob_theme_current() → lazy init fallback (idempotent first-wins,
     * provider absent). The vendored theme engine keeps colors/layouts
     * in ONE global rc shim: only one theme instance may exist per
     * process, and each decorated view constructs a decoration_theme_t.
     * The old per-view init + finish pair nulled rc.theme on the first
     * destructor and crashed hover rendering on the rest. */
    if (auto data = wf::get_core().get_data<obtheme::ob_theme_data_t>(
        "ob-theme"))
    {
        ob = data->theme;
    }
    else
    {
        ob = ob_theme_current();
    }

    if (!ob)
    {
        struct ob_theme_config cfg = {};
        std::string name  = theme_name;
        std::string fnt   = font;
        std::string lay   = "menu:iconify,max,close";
        cfg.theme_name      = name.c_str();
        cfg.font            = fnt.c_str();
        cfg.titlebar_layout = lay.c_str();
        ob_theme_init(&ob, cfg);
    }
}

/* False exactly once per in-place reload (ob-theme provider bumps the
 * epoch); adopts the new baseline so subsequent calls are true again —
 * callers rebuild their caches on false. */
bool decoration_theme_t::epoch_valid() const
{
    uint32_t e = 0;
    if (auto data = wf::get_core().get_data<obtheme::ob_theme_data_t>(
        DATA_NAME))
    {
        e = data->epoch;
    }

    if (epoch_seen_ == UINT32_MAX)
    {
        epoch_seen_ = e;
    }

    if (e != epoch_seen_)
    {
        epoch_seen_ = e;
        return false;
    }

    return true;
}

decoration_theme_t::~decoration_theme_t()
{
    /* singleton lifetime = process; do NOT ob_theme_finish here (it
     * nulls the shared rc.theme for every other consumer) */
}

int decoration_theme_t::get_title_height() const
{
    return ob ? ob_theme_titlebar_height(ob) : 30;
}

int decoration_theme_t::get_border_size() const
{
    return ob ? ob_theme_border_width(ob) : 4;
}

int decoration_theme_t::get_ring_width() const
{
    return ring_w;
}

wf::color_t decoration_theme_t::get_ring_color(bool active) const
{
    return active ? ring_active_color.value() : ring_inactive_color.value();
}

void decoration_theme_t::ensure_fill_texture(int active) const
{
    auto *surf = ob_theme_titlebar_fill(ob, active);
    if (!surf)
    {
        return;
    }

    /* fill_src pointer comparison also catches reloads (theme_finish
     * rebuilds the cairo surface), but the pointer value can be reused
     * by the allocator after a free — the epoch check is authoritative. */
    if (!epoch_valid() || (fill_src[active] != surf) ||
        !fill_tex[active].get_texture())
    {
        fill_tex[active] = wf::owned_texture_t{surf};
        fill_src[active] = surf;
    }
}

void decoration_theme_t::render_background(
    const wf::scene::render_instruction_t& data, wf::geometry_t frame,
    bool active) const
{
    if ((frame.width <= 0) || (frame.height <= 0))
    {
        return;
    }

    const int b  = get_border_size();
    const int th = std::min(get_title_height(), (int)frame.height);

    /* labwc geometry: frame = [ring] inner rect; border wraps ALL sides, the
     * titlebar sits INSIDE the border. Paint the whole frame in the border
     * color first (yields all 4 border strips), then overlay the titlebar
     * fill inset by the border — this leaves no gap between titlebar and
     * client content. */
    const float *c = ob_theme_border_color(ob, active);
    wf::color_t bc(c[0], c[1], c[2], c[3]);
    data.pass->add_rect(bc, data.target, frame, data.damage);

    const int tb_w = std::max(0, (int)frame.width - 2 * b);
    const int tb_h = std::min(th, (int)frame.height);
    if ((tb_w > 0) && (tb_h > 0))
    {
        auto texture = fill_tex[active].get_texture();
        if (texture)
        {
            wf::geometry_t tb{frame.x + b, frame.y, (double)tb_w,
                (double)tb_h};
            data.pass->add_texture(texture, data.target, tb, data.damage);
        }
    }
}

void decoration_theme_t::render_ring(
    const wf::scene::render_instruction_t& data, wf::geometry_t outer,
    bool active) const
{
    const int rw = get_ring_width();
    if ((rw <= 0) || (outer.width <= 0) || (outer.height <= 0))
    {
        return;
    }

    const wf::color_t &rc = get_ring_color(active);
    const wf::geometry_t top{outer.x, outer.y, outer.width, (double)rw};
    const wf::geometry_t bottom{outer.x, outer.y + outer.height - rw,
        outer.width, (double)rw};
    const double side_height = std::max(0.0, outer.height - 2.0 * rw);
    const wf::geometry_t left{outer.x, outer.y + rw, (double)rw, side_height};
    const wf::geometry_t right{outer.x + outer.width - rw, outer.y + rw,
        (double)rw, side_height};
    data.pass->add_rect(rc, data.target, top, data.damage);
    data.pass->add_rect(rc, data.target, bottom, data.damage);
    data.pass->add_rect(rc, data.target, left, data.damage);
    data.pass->add_rect(rc, data.target, right, data.damage);
}

int decoration_theme_t::get_title_buttons(int side, enum lab_node_type *out,
    int max) const
{
    return ob_theme_title_buttons(ob, side, out, max);
}

void decoration_theme_t::get_button_size(int *width, int *height,
    int *spacing) const
{
    ob_theme_button_size(ob, width, height, spacing);
}

std::shared_ptr<wf::texture_t> decoration_theme_t::get_button_texture(
    enum lab_node_type type, unsigned bits, int active) const
{
    int bw, bh, bs;
    get_button_size(&bw, &bh, &bs);
    const int key = ((int)type << 8) | ((int)(bits & 3) << 1) | (active & 1);
    auto it = button_tex_cache.find(key);
    if (!epoch_valid() || (it == button_tex_cache.end()))
    {
        auto *surf = ob_theme_render_button(ob, type, bits, active, bw, bh,
            1.0);
        if (!surf)
        {
            return {};
        }

        it = button_tex_cache.emplace(key, wf::owned_texture_t{surf}).first;
        cairo_surface_destroy(surf);
    }

    return it->second.get_texture();
}

std::string decoration_theme_t::title_font(bool active) const
{
    /* labwc rc.xml <font place="ActiveWindow"> semantics: separate
     * per-focus fonts; empty option falls back to ob-decor/font */
    auto &opt = active ? font_active : font_inactive;
    if (!((std::string)opt).empty())
    {
        return opt;
    }

    return font;
}

cairo_surface_t *decoration_theme_t::render_text(std::string text,
    int width, int height, bool active) const
{
    const auto format = CAIRO_FORMAT_ARGB32;
    auto surface = cairo_image_surface_create(format, width, height);

    if ((height == 0) || (width == 0))
    {
        return surface;
    }

    const float *fc = ob_theme_label_text_color(ob, active);
    wf::color_t color(fc[0], fc[1], fc[2], fc[3]);
    auto cr = cairo_create(surface);

    PangoFontDescription *font_desc =
        pango_font_description_from_string(title_font(active).c_str());
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, text.c_str(), text.size());
    /* Justify within the whole title area, per the theme. */
    pango_layout_set_width(layout, width * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    switch (ob_theme_justify(ob))
    {
      case LAB_JUSTIFY_CENTER:
        pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
        break;

      case LAB_JUSTIFY_RIGHT:
        pango_layout_set_alignment(layout, PANGO_ALIGN_RIGHT);
        break;

      default:
        pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
        break;
    }

    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(font_desc);
    g_object_unref(layout);
    cairo_destroy(cr);

    return surface;
}

}
}
