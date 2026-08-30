#include "deco-theme.hpp"
#include <wayfire/opengl.hpp>
#include <pango/pangocairo.h>
#include <algorithm>

namespace wf
{
namespace odecor
{
decoration_theme_t::decoration_theme_t()
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

decoration_theme_t::~decoration_theme_t()
{
    ob_theme_finish(ob);
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

void decoration_theme_t::set_buttons(button_type_t flags)
{
    button_flags = flags;
}

void decoration_theme_t::ensure_fill_texture(int active) const
{
    auto *surf = ob_theme_titlebar_fill(ob, active);
    if (!surf)
    {
        return;
    }

    if ((fill_src[active] != surf) ||
        !fill_tex[active].get_texture().texture)
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

    /* Titlebar strip: theme fill (solid or vertical/splitvertical gradient),
     * baked into a 1px-wide surface and stretched horizontally. */
    if (th > 0)
    {
        ensure_fill_texture(active);
        if (fill_tex[active].get_texture().texture)
        {
            wf::geometry_t tb{frame.x, frame.y, frame.width, th};
            data.pass->add_texture(fill_tex[active].get_texture(), data.target,
                tb, data.damage);
        }
    }

    /* Border strips in the theme border color. */
    if (b > 0)
    {
        const float *c = ob_theme_border_color(ob, active);
        wf::color_t bc(c[0], c[1], c[2], c[3]);
        wf::geometry_t left{frame.x, frame.y + th, (uint32_t)b,
            (uint32_t)(frame.height - th)};
        wf::geometry_t right{frame.x + frame.width - b, frame.y + th,
            (uint32_t)b, (uint32_t)(frame.height - th)};
        wf::geometry_t bottom{frame.x, frame.y + frame.height - b,
            (uint32_t)frame.width, (uint32_t)b};
        data.pass->add_rect(bc, data.target, left, data.damage);
        data.pass->add_rect(bc, data.target, right, data.damage);
        data.pass->add_rect(bc, data.target, bottom, data.damage);
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
    wf::geometry_t top{outer.x, outer.y, outer.width, (uint32_t)rw};
    wf::geometry_t bottom{outer.x, outer.y + outer.height - rw,
        outer.width, (uint32_t)rw};
    wf::geometry_t left{outer.x, outer.y + rw, (uint32_t)rw,
        (uint32_t)(outer.height - 2 * rw)};
    wf::geometry_t right{outer.x + outer.width - rw, outer.y + rw,
        (uint32_t)rw, (uint32_t)(outer.height - 2 * rw)};
    data.pass->add_rect(rc, data.target, top, data.damage);
    data.pass->add_rect(rc, data.target, bottom, data.damage);
    data.pass->add_rect(rc, data.target, left, data.damage);
    data.pass->add_rect(rc, data.target, right, data.damage);
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
        pango_font_description_from_string(((std::string)font).c_str());
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

/* T4 will replace this with theme-image buttons; until then the old
 * wayfire-style glyphs are kept (unused while button layout is empty). */
cairo_surface_t *decoration_theme_t::get_button_surface(button_type_t button,
    const button_state_t& state) const
{
    cairo_surface_t *button_surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, state.width, state.height);

    auto cr = cairo_create(button_surface);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_rectangle(cr, 0, 0, state.width, state.height);
    cairo_fill(cr);

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    color_t base = {0.60, 0.60, 0.63, 0.36};
    double line  = 0.27;
    double hover = 0.27;

    if (fabs(state.hover_progress) > 1e-3)
    {
        switch (button)
        {
          case BUTTON_CLOSE:
            base = {242.0 / 255.0, 80.0 / 255.0, 86.0 / 255.0, 0.63};
            break;

          case BUTTON_TOGGLE_MAXIMIZE:
            base = {57.0 / 255.0, 234.0 / 255.0, 73.0 / 255.0, 0.63};
            break;

          case BUTTON_MINIMIZE:
            base = {250.0 / 255.0, 198.0 / 255.0, 54.0 / 255.0, 0.63};
            break;

          default:
            assert(false);
        }

        line *= 2.0;
    }

    cairo_set_source_rgba(cr,
        base.r + 0.0 * state.hover_progress,
        base.g + 0.0 * state.hover_progress,
        base.b + 0.0 * state.hover_progress,
        base.a + hover * state.hover_progress);
    cairo_arc(cr, state.width / 2, state.height / 2,
        state.width / 2, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_set_line_width(cr, state.border);
    cairo_set_source_rgba(cr, 0.00, 0.00, 0.00, line);
    double r = state.width / 2 - 0.5 * state.border;
    cairo_arc(cr, state.width / 2, state.height / 2, r, 0, 2 * M_PI);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, 0.00, 0.00, 0.00, line / 2);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    switch (button)
    {
      case BUTTON_CLOSE:
        cairo_set_line_width(cr, 1.5 * state.border);
        cairo_move_to(cr, 1.0 * state.width / 4.0,
            1.0 * state.height / 4.0);
        cairo_line_to(cr, 3.0 * state.width / 4.0,
            3.0 * state.height / 4.0);
        cairo_move_to(cr, 3.0 * state.width / 4.0,
            1.0 * state.height / 4.0);
        cairo_line_to(cr, 1.0 * state.width / 4.0,
            3.0 * state.height / 4.0);
        cairo_stroke(cr);
        break;

      case BUTTON_TOGGLE_MAXIMIZE:
        cairo_set_line_width(cr, 1.5 * state.border);
        cairo_rectangle(cr, state.width / 4.0, state.height / 4.0,
            state.width / 2.0, state.height / 2.0);
        cairo_stroke(cr);
        break;

      case BUTTON_MINIMIZE:
        cairo_set_line_width(cr, 1.75 * state.border);
        cairo_move_to(cr, 1.0 * state.width / 4.0, state.height / 2.0);
        cairo_line_to(cr, 3.0 * state.width / 4.0, state.height / 2.0);
        cairo_stroke(cr);
        break;

      default:
        assert(false);
    }

    cairo_fill(cr);
    cairo_destroy(cr);

    return button_surface;
}
}
}
