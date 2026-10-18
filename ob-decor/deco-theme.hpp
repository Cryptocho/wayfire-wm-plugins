#pragma once
#include <wayfire/render-manager.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/config/types.hpp>
#include "deco-button.hpp"
#include <map>
#include <cstdint>
#include <obtheme.h>
#include "../ob-theme/ob-theme-data.hpp"

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

    /** Parsed titlebar buttons for side (0 = left, 1 = right); @returns count */
    int get_title_buttons(int side, enum lab_node_type *out, int max) const;
    /** Theme-configured button geometry (window.button.* keys) */
    void get_button_size(int *width, int *height, int *spacing) const;

    /**
     * Cached GL texture of the theme image for the given button
     * (state bits: OB_BTN_*). Size follows window.button.*.
     * Missing variants fall back like labwc; may be an empty texture.
     */
    std::shared_ptr<wf::texture_t> get_button_texture(enum lab_node_type type,
        unsigned bits, int active) const;

  private:
    struct theme *ob = nullptr;

    /* ob-theme provider linkage: borrowed singleton pointer + in-place
     * reload detection. ob_theme_reinit() reparses the SAME struct, so
     * @ob stays valid; texture caches must drop on epoch change. */
    bool epoch_valid() const; /* false exactly once per reload */
    mutable uint32_t epoch_seen_ = UINT32_MAX;
    static constexpr const char *DATA_NAME = "ob-theme";

    wf::option_wrapper_t<std::string> theme_name{"ob-decor/theme"};
    wf::option_wrapper_t<std::string> font{"ob-decor/font"};
    wf::option_wrapper_t<std::string> font_active{"ob-decor/font_active"};
    wf::option_wrapper_t<std::string> font_inactive{"ob-decor/font_inactive"};
    /* per-focus title font: explicit option, falling back to @font */
    std::string title_font(bool active) const;
    wf::option_wrapper_t<int> ring_w{"ob-decor/ring_width"};
    wf::option_wrapper_t<wf::color_t> ring_active_color{"ob-decor/ring_active_color"};
    wf::option_wrapper_t<wf::color_t> ring_inactive_color{"ob-decor/ring_inactive_color"};

    /* Cached GL texture of the 1px titlebar_fill surface, per state. */
    void ensure_fill_texture(int active) const;
    mutable wf::owned_texture_t fill_tex[2];
    mutable cairo_surface_t *fill_src[2] = {nullptr, nullptr};

    /* Button texture cache; key = (lab_type << 8) | (bits << 1) | active. */
    mutable std::map<int, wf::owned_texture_t> button_tex_cache;
};
}
}
