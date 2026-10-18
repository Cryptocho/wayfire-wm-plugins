#include "wayfire/geometry.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/toplevel.hpp"
#include <memory>
#define GLM_FORCE_RADIANS
#include <glm/gtc/matrix_transform.hpp>

#include <linux/input-event-codes.h>

#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include "deco-subsurface.hpp"
#include "deco-layout.hpp"
#include "deco-theme.hpp"
#include "ob-menu-events.hpp"
#include <wayfire/window-manager.hpp>

#include <wayfire/plugins/common/cairo-util.hpp>

#include <cairo.h>

class simple_decoration_node_t : public wf::scene::node_t, public wf::pointer_interaction_t,
    public wf::touch_interaction_t
{
    std::weak_ptr<wf::toplevel_view_interface_t> _view;
    wf::signal::connection_t<wf::view_title_changed_signal> title_set =
        [=] (wf::view_title_changed_signal *ev)
    {
        if (auto view = _view.lock())
        {
            view->damage();
        }
    };

    void update_title(int width, int height, double scale, bool active)
    {
        if (auto view = _view.lock())
        {
            wf::dimensions_t target_size = {
                static_cast<int32_t>(width * scale),
                static_cast<int32_t>(height * scale)
            };

            if ((title_texture.tex.get_size() != target_size) ||
                (title_texture.current_text != view->get_title()) ||
                (title_texture.current_active != active))
            {
                auto surface = theme.render_text(view->get_title(), target_size.width, target_size.height, active);
                title_texture.tex = wf::owned_texture_t{surface};
                cairo_surface_destroy(surface);
                title_texture.current_text = view->get_title();
            }
        }
    }

    struct
    {
        wf::owned_texture_t tex;
        std::string current_text = "";
        bool current_active = false;
    } title_texture;

  public:
    wf::odecor::decoration_theme_t theme;
    wf::odecor::decoration_layout_t layout;
    wf::region_t cached_region;

    wf::dimensions_t size;

    int current_thickness;
    int current_titlebar;

    simple_decoration_node_t(wayfire_toplevel_view view) :
        node_t(false),
        theme{},
        layout{theme, [=] (wlr_box box)
        {
            wf::scene::damage_node(shared_from_this(),
                wf::from_integer_box(box) + wf::pointf_t{get_offset()});
        }}
    {
        this->_view = view->weak_from_this();
        view->connect(&title_set);

        // make sure to hide frame if the view is fullscreen
        update_decoration_size();
    }

    wf::point_t get_offset()
    {
        return {-current_thickness, -current_titlebar};
    }

    void render(const wf::scene::render_instruction_t& data)
    {
        auto origin = get_offset();
        /* Clear background */
        wf::geometry_t geometry = wf::construct_box(
            wf::pointf_t{origin}, wf::dimensionsf_t{size.width, size.height});

        bool activated = false;
        if (auto view = _view.lock())
        {
            activated = view->activated;
        }

        /* Outer ring first, then the themed frame inside it. */
        theme.render_ring(data, geometry, activated);
        const int rw = theme.get_ring_width();
        wf::geometry_t inner = geometry;
        inner.x += rw;
        inner.y += rw;
        inner.width  -= 2 * rw;
        inner.height -= 2 * rw;
        theme.render_background(data, inner, activated);

        /* Draw title & buttons */
        auto renderables = layout.get_renderable_areas();
        for (auto item : renderables)
        {
            if (item->get_type() == wf::odecor::DECORATION_AREA_TITLE)
            {
                wf::geometry_t title_geometry =
                    item->get_geometry() + wf::pointf_t{origin};
                update_title(title_geometry.width, title_geometry.height,
                    data.target.scale, activated);
                auto texture = title_texture.tex.get_texture();
                if (texture)
                {
                    data.pass->add_texture(texture, data.target,
                        title_geometry, data.damage);
                }
            } else // button
            {
                item->as_button().render(data,
                    item->get_geometry() + wf::pointf_t{origin}, activated);
            }
        }
    }

    std::optional<wf::scene::input_node_t> find_node_at(const wf::pointf_t& at) override
    {
        if (auto view = _view.lock())
        {
            wf::pointf_t local = at - wf::pointf_t{get_offset()};
            if (cached_region.contains_pointf(local) && view->is_mapped())
            {
                return wf::scene::input_node_t{
                    .node = this,
                    .local_coords = local,
                };
            }
        }

        return {};
    }

    pointer_interaction_t& pointer_interaction() override
    {
        return *this;
    }

    touch_interaction_t& touch_interaction() override
    {
        return *this;
    }

    class decoration_render_instance_t : public wf::scene::render_instance_t
    {
        std::shared_ptr<simple_decoration_node_t> self;
        wf::scene::damage_callback push_damage;

        wf::signal::connection_t<wf::scene::node_damage_signal> on_surface_damage =
            [=] (wf::scene::node_damage_signal *data)
        {
            push_damage(data->region);
        };

      public:
        decoration_render_instance_t(simple_decoration_node_t *self, wf::scene::damage_callback push_damage)
        {
            this->self = std::dynamic_pointer_cast<simple_decoration_node_t>(self->shared_from_this());
            this->push_damage = push_damage;
            self->connect(&on_surface_damage);
        }

        void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::regionf_t& damage) override
        {
            auto our_region = wf::regionf_t{self->cached_region} +
                wf::pointf_t{self->get_offset()};
            wf::regionf_t our_damage = damage & our_region;
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
            self->render(data);
        }
    };

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *output = nullptr) override
    {
        instances.push_back(std::make_unique<decoration_render_instance_t>(this, push_damage));
    }

    wf::geometry_t get_bounding_box() override
    {
        return wf::construct_box(wf::pointf_t{get_offset()},
            wf::dimensionsf_t{size.width, size.height});
    }
    /* wf::compositor_surface_t implementation */
    void handle_pointer_enter(wf::pointf_t point) override
    {
        point -= wf::pointf_t{get_offset()};
        layout.handle_motion(point.x, point.y);
    }

    void handle_pointer_leave() override
    {
        layout.handle_focus_lost();
    }

    void handle_pointer_motion(wf::pointf_t to, uint32_t) override
    {
        to -= wf::pointf_t{get_offset()};
        handle_action(layout.handle_motion(to.x, to.y));
    }

    void handle_pointer_button(const wlr_pointer_button_event& ev) override
    {
        if (ev.button == BTN_RIGHT)
        {
            /* labwc: right-click on the titlebar opens the client-menu */
            if ((ev.state == WL_POINTER_BUTTON_STATE_PRESSED) &&
                layout.pointer_in_titlebar())
            {
                request_client_menu();
            }

            return;
        }

        if (ev.button != BTN_LEFT)
        {
            return;
        }

        handle_action(layout.handle_press_event(ev.state == WL_POINTER_BUTTON_STATE_PRESSED));
    }

    void handle_action(wf::odecor::decoration_layout_t::action_response_t action)
    {
        if (auto view = _view.lock())
        {
            switch (action.action)
            {
              case wf::odecor::DECORATION_ACTION_MOVE:
                return wf::get_core().default_wm->move_request(view);

              case wf::odecor::DECORATION_ACTION_RESIZE:
                return wf::get_core().default_wm->resize_request(view, action.edges);

              case wf::odecor::DECORATION_ACTION_CLOSE:
                return view->close();

              case wf::odecor::DECORATION_ACTION_TOGGLE_MAXIMIZE:
                if (view->pending_tiled_edges())
                {
                    return wf::get_core().default_wm->tile_request(view, 0);
                } else
                {
                    return wf::get_core().default_wm->tile_request(view, wf::TILED_EDGES_ALL);
                }

                break;

              case wf::odecor::DECORATION_ACTION_MINIMIZE:
                return wf::get_core().default_wm->minimize_request(view, true);
                break;

              case wf::odecor::DECORATION_ACTION_MENU:
                return request_client_menu();

              default:
                break;
            }
        }
    }

    /** Fire ob_menu_request_signal ("client-menu") on the view's output. */
    void request_client_menu()
    {
        if (auto view = _view.lock())
        {
            auto output = view->get_output();
            obmenu::ob_menu_request_signal req;
            req.menu_id     = "client-menu";
            req.client_view = view;
            req.anchor      = output->get_cursor_position();
            output->emit(&req);
        }
    }

    void handle_touch_down(uint32_t time_ms, int finger_id, wf::pointf_t position) override
    {
        handle_touch_motion(time_ms, finger_id, position);
        handle_action(layout.handle_press_event());
    }

    void handle_touch_up(uint32_t time_ms, int finger_id, wf::pointf_t lift_off_position) override
    {
        handle_action(layout.handle_press_event(false));
        layout.handle_focus_lost();
    }

    void handle_touch_motion(uint32_t time_ms, int finger_id, wf::pointf_t position) override
    {
        position -= wf::pointf_t{get_offset()};
        handle_action(layout.handle_motion(position.x, position.y));
    }

    void resize(wf::dimensions_t dims)
    {
        if (auto view = _view.lock())
        {
            view->damage();
            size = dims;
            layout.resize(size.width, size.height);
            if (!view->toplevel()->current().fullscreen)
            {
                this->cached_region = layout.calculate_region();
            }

            view->damage();
        }
    }

    void update_decoration_size()
    {
        bool fullscreen = _view.lock()->toplevel()->current().fullscreen;
        if (fullscreen)
        {
            current_thickness = 0;
            current_titlebar  = 0;
            this->cached_region.clear();
        } else
        {
            current_thickness = theme.get_border_size() + theme.get_ring_width();
            current_titlebar  = theme.get_title_height() +
                theme.get_border_size() + theme.get_ring_width();
            this->cached_region = layout.calculate_region();
        }
    }
};

wf::ob_decorator_t::ob_decorator_t(wayfire_toplevel_view view)
{
    this->view = view;
    deco = std::make_shared<simple_decoration_node_t>(view);
    deco->resize(wf::dimensions(view->get_pending_geometry()));
    wf::scene::add_back(view->get_surface_root_node(), deco);

    view->connect(&on_view_activated);
    view->connect(&on_view_geometry_changed);
    view->connect(&on_view_fullscreen);

    on_view_activated = [this] (auto)
    {
        wf::scene::damage_node(deco, deco->get_bounding_box());
    };

    on_view_geometry_changed = [this] (auto)
    {
        deco->resize(wf::dimensions(this->view->get_geometry()));
    };

    on_view_fullscreen = [this] (auto)
    {
        deco->update_decoration_size();
        if (!this->view->toplevel()->current().fullscreen)
        {
            deco->resize(wf::dimensions(this->view->get_geometry()));
        }
    };
}

wf::ob_decorator_t::~ob_decorator_t()
{
    wf::scene::remove_child(deco);
}

int wf::ob_decorator_t::simulate_drag(int x0, int y0, int x1, int y1)
{
    auto& layout = deco->layout;
    layout.handle_motion(x0, y0);
    auto press = layout.handle_press_event(true);
    if (press.action != wf::odecor::DECORATION_ACTION_NONE)
    {
        return press.action; /* immediate action (resize) */
    }

    for (int i = 1; i <= 10; i++)
    {
        int xi = x0 + (x1 - x0) * i / 10;
        int yi = y0 + (y1 - y0) * i / 10;
        auto act = layout.handle_motion(xi, yi);
        if (act.action != wf::odecor::DECORATION_ACTION_NONE)
        {
            layout.handle_focus_lost();
            return act.action;
        }
    }

    auto release = layout.handle_press_event(false);
    layout.handle_focus_lost();
    return release.action;
}

wf::decoration_margins_t wf::ob_decorator_t::get_margins(const wf::toplevel_state_t& state)
{
    if (state.fullscreen)
    {
        return {0, 0, 0, 0};
    }

    const int thickness = deco->theme.get_border_size() +
        deco->theme.get_ring_width();
    const int titlebar  = deco->theme.get_title_height() +
        deco->theme.get_border_size() + deco->theme.get_ring_width();
    return wf::decoration_margins_t{
        .left   = thickness,
        .right  = thickness,
        .bottom = thickness,
        .top    = titlebar,
    };
}
