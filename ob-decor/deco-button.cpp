#include "deco-button.hpp"
#include "deco-theme.hpp"
#include <wayfire/debug.hpp>

namespace wf
{
namespace odecor
{
button_t::button_t(const decoration_theme_t& t, std::function<void()> damage) :
    theme(t), damage_callback(damage)
{}

void button_t::set_lab_type(enum lab_node_type type)
{
    this->lab_type = type;
    add_idle_damage();
}

enum lab_node_type button_t::get_lab_type() const
{
    return this->lab_type;
}

void button_t::set_action(int action)
{
    this->action_id = action;
}

int button_t::get_action() const
{
    return this->action_id;
}

void button_t::set_hover(bool is_hovered)
{
    if (this->is_hovered != is_hovered)
    {
        this->is_hovered = is_hovered;
        add_idle_damage();
    }
}

void button_t::set_pressed(bool is_pressed)
{
    if (this->is_pressed != is_pressed)
    {
        this->is_pressed = is_pressed;
        add_idle_damage();
    }
}

void button_t::render(const scene::render_instruction_t& data,
    wf::geometry_t geometry, bool active)
{
    /* Pressed shows the hovered variant until T5 wires PRESSED colors. */
    const unsigned bits =
        (is_hovered || is_pressed) ? OB_BTN_HOVERED : OB_BTN_DEFAULT;
    auto tex = theme.get_button_texture(lab_type, bits, active);
    if (tex)
    {
        data.pass->add_texture(tex, data.target, geometry, data.damage);
    }
}

void button_t::add_idle_damage()
{
    this->idle_damage.run_once([=] ()
    {
        this->damage_callback();
    });
}
}
}
