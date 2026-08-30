#pragma once

#include <wayfire/scene-render.hpp>
#include <wayfire/util.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

#include <cairo.h>

#include <obtheme.h>

namespace wf
{
namespace odecor
{
class decoration_theme_t;

class button_t
{
  public:
    /**
     * Create a new button with the given theme.
     * @param theme  The theme to use.
     * @param damage_callback   A callback to execute when the button needs a
     * repaint.
     */
    button_t(const decoration_theme_t& theme,
        std::function<void()> damage_callback);

    /** The labwc button identity (drives the rendered image). */
    void set_lab_type(enum lab_node_type type);
    enum lab_node_type get_lab_type() const;

    /** Action id from decoration_layout_action_t; 0 = inert button. */
    void set_action(int action);
    int get_action() const;

    /** Hover/press state; affects which theme image variant is used. */
    void set_hover(bool is_hovered);
    void set_pressed(bool is_pressed);

    /**
     * Render the button on the given framebuffer at the given coordinates.
     *
     * @param data The render data
     * @param geometry The geometry of the button, in logical coordinates
     * @param active Whether the owning view is activated
     */
    void render(const scene::render_instruction_t& data,
        wf::geometry_t geometry, bool active);

  private:
    const decoration_theme_t& theme;

    enum lab_node_type lab_type = LAB_NODE_BUTTON_CLOSE;
    int action_id = 0;
    bool is_hovered = false;
    bool is_pressed = false;

    std::function<void()> damage_callback;
    wf::wl_idle_call idle_damage;
    /** Damage button the next time the main loop goes idle */
    void add_idle_damage();
};
}
}
