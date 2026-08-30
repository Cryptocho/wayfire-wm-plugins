#pragma once
#include <wayfire/render-manager.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/config/types.hpp>
#include "deco-button.hpp"
#include <obtheme.h>

namespace wf
{
namespace odecor
{
/**
 * Theme engine backed by libobtheme (openbox themerc + labwc theme engine).
 * Draws the titlebar strip (solid or gradient fill), the border strips and
 * the outer "ring" decoration on top of everything.
 */
class decoration_theme_t
{
  public:
    decoration_theme_t();
    ~decoration_theme_t();

    /** @return The available height for displaying the title */
    int get_title_height() const;
    /** @return The theme border width around the client */
    int get_border_size() const;
    /** @return The extra outer ring width (drawn outside the border) */
    int get_ring_width() const;
    /** @return Ring color for the given activation state */
    wf::color_t get_ring_color(bool active) const;

    /** Set the flags for buttons */
    void set_buttons(button_type_t flags);
    button_type_t button_flags;

    /**
     * Draw the themed frame (titlebar strip + border strips) inside @frame,
     * i.e. the window rectangle excluding the outer ring.
     */
    void render_background(const wf::scene::render_instruction_t& data,
        wf::geometry_t frame, bool active) const;

    /** Draw the outer ring strips covering @outer. */
    void render_ring(const wf::scene::render_instruction_t& data,
        wf::geometry_t outer, bool active) const;

    /**
     * Render the title text on a cairo_surface_t (justified per theme).
     * The caller is responsible for freeing the memory afterwards.
     */
    cairo_surface_t *render_text(std::string text, int width, int height,
        bool active) const;

    struct button_state_t
    {
        double width;
        double height;
        double border;
        double hover_progress;
    };

    /* T4 will replace this with theme-image buttons. */
    cairo_surface_t *get_button_surface(button_type_t button,
        const button_state_t& state) const;

  private:
    struct theme *ob = nullptr;

    wf::option_wrapper_t<std::string> theme_name{"ob-decor/theme"};
    wf::option_wrapper_t<std::string> font{"ob-decor/font"};
    wf::option_wrapper_t<int> ring_w{"ob-decor/ring_width"};
    wf::option_wrapper_t<wf::color_t> ring_active_color{"ob-decor/ring_active_color"};
    wf::option_wrapper_t<wf::color_t> ring_inactive_color{"ob-decor/ring_inactive_color"};

    /* Cached GL texture of the 1px titlebar_fill surface, per state. */
    void ensure_fill_texture(int active) const;
    mutable wf::owned_texture_t fill_tex[2];
    mutable cairo_surface_t *fill_src[2] = {nullptr, nullptr};
};
}
}
