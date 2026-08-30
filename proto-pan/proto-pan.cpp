#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/compositor-view.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/bindings-repository.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/unstable/translation-node.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/common/input-grab.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/debug.hpp>

/**
 * proto-pan: P0 prototype for the scrolling layout's core risk mechanism —
 * a plugin-owned scene sublayer whose content can be freely panned.
 *
 * S4: continuous pan driven by touchpad swipe gestures (vswipe pattern) on
 * top of the S3 keyboard step-pan. The pan control is factored into
 * begin/update/end functions shared by both input paths and by the
 * proto-pan/drag IPC method (used for automated tests, since gestures
 * cannot be injected headlessly).
 *
 * Pan model: rail of N_TILES screens. Content displacement:
 *   content_x = clamp(drag_px - tile_index * W, -(N_TILES-1)*W, 0)
 * where drag_px follows the fingers (natural scrolling: fingers right =
 * content right). On gesture end, snap to the nearest tile.
 */
class proto_pan_output_t : public wf::per_output_plugin_instance_t
{
  public:
    void init() override
    {
        LOGI("proto-pan init on ", output->to_string());
        setup_pan_rail();

        output->add_key(key_left, &on_step_left);
        output->add_key(key_right, &on_step_right);
        output->connect(&on_view_mapped);

        input_grab = std::make_unique<wf::input_grab_t>("proto-pan", output);
        wf::get_core().connect(&on_swipe_begin);
        wf::get_core().connect(&on_swipe_update);
        wf::get_core().connect(&on_swipe_end);

        ipc_repo->register_method("proto-pan/step", [this] (wf::json_t data)
        {
            return ipc_step(data);
        });
        ipc_repo->register_method("proto-pan/drag", [this] (wf::json_t data)
        {
            return ipc_drag(data);
        });
        ipc_repo->register_method("proto-pan/get-state", [this] (wf::json_t)
        {
            return get_state_json();
        });
    }

    void fini() override
    {
        ipc_repo->unregister_method("proto-pan/step");
        ipc_repo->unregister_method("proto-pan/drag");
        ipc_repo->unregister_method("proto-pan/get-state");
        cancel_pan();
        settle_animation();
        teardown_pan_rail();
        LOGI("proto-pan fini on ", output->to_string());
    }

  private:
    static constexpr int N_TILES = 3;

    /* The pan rail: all pannable content lives inside this node; panning =
     * changing its offset. translation_node_t is-a floating_inner_node_t, so
     * it holds children directly. */
    std::shared_ptr<wf::scene::translation_node_t> pan_node;
    std::vector<std::shared_ptr<wf::color_rect_view_t>> rects;

    /* Pan state: current tile index + continuous drag (px, follows fingers). */
    int tile_index = 0;
    int drag_px = 0;
    double drag_accum = 0.0; /* sub-pixel accumulator while swiping */
    bool swiping = false;

    /* First toplevel adopted into the rail (prototype handles exactly one). */
    bool has_pan_view = false;
    wayfire_toplevel_view pan_view;

    wf::option_wrapper_t<wf::keybinding_t> key_left{"proto-pan/key_left"};
    wf::option_wrapper_t<wf::keybinding_t> key_right{"proto-pan/key_right"};
    wf::key_callback on_step_left  = [=] (auto) { step(-1); return true; };
    wf::key_callback on_step_right = [=] (auto) { step(+1); return true; };

    wf::option_wrapper_t<int> swipe_fingers{"proto-pan/swipe_fingers"};

    /* Smooth pan animation (vswitch pattern, plugins/vswitch/vswitch.cpp). */
    struct pan_animation_t : public wf::animation::duration_t
    {
        using duration_t::duration_t;
        wf::animation::timed_transition_t dx{*this};
    };

    pan_animation_t animation{
        wf::option_wrapper_t<wf::animation_description_t>{"proto-pan/duration"}};

    /** Advance the animation each frame; stop the hook when done. */
    wf::effect_hook_t on_frame = [=] ()
    {
        if (animation.running())
        {
            set_offset_raw((int)std::round(animation.dx));
            output->render->schedule_redraw();
        } else
        {
            output->render->rem_effect(&on_frame);
            LOGI("proto-pan animation done: offset=", pan_node->get_offset().x);
        }
    };

    /* Animate the content displacement from @from_x to @target_x. */
    void animate_content_from(int from_x, int target_x)
    {
        animation.dx.set(from_x, target_x);
        animation.start();
        output->render->add_effect(&on_frame, wf::OUTPUT_EFFECT_PRE);
        output->render->schedule_redraw();
    }

    /** If a pan animation is in flight, land it at the nearest tile now. */
    void settle_animation()
    {
        if (!animation.running())
        {
            return;
        }

        output->render->rem_effect(&on_frame);
        auto W = output->get_screen_size().width;
        tile_index = std::clamp((int)std::round(-(double)pan_node->get_offset().x / W),
            0, N_TILES - 1);
        drag_px    = 0;
        drag_accum = 0.0;
        apply_offset();
    }

    /* Input grab during a swipe (vswipe pattern, plugins/single_plugins/vswipe.cpp). */
    std::unique_ptr<wf::input_grab_t> input_grab;
    wf::plugin_activation_data_t grab_interface = {
        .name = "proto-pan",
        .capabilities = wf::CAPABILITY_GRAB_INPUT,
        .cancel = [=] () { cancel_pan(); },
    };

    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped =
        [=] (wf::view_mapped_signal *ev)
    {
        auto toplevel = wf::toplevel_cast(ev->view);
        if (!toplevel || toplevel->parent || has_pan_view)
        {
            return;
        }

        has_pan_view = true;
        pan_view     = toplevel;
        /* Park it on tile #1 so panning has something real to reveal. */
        auto size = output->get_screen_size();
        toplevel->move(size.width, 0);
        wf::scene::readd_front(pan_node, toplevel->get_root_node());
        LOGI("proto-pan: adopted view '", toplevel->get_title(),
            "' into the rail at x=", size.width);
    };

    template<class wlr_event>
    using event = wf::input_event_signal<wlr_event>;

    wf::signal::connection_t<event<wlr_pointer_swipe_begin_event>> on_swipe_begin =
        [=] (event<wlr_pointer_swipe_begin_event> *ev)
    {
        if ((int)ev->event->fingers != swipe_fingers)
        {
            return;
        }

        /* Plugins are per-output; handle the swipe only on our output. */
        if (!(output->get_relative_geometry() & output->get_cursor_position()))
        {
            return;
        }

        begin_pan();
    };

    wf::signal::connection_t<event<wlr_pointer_swipe_update_event>> on_swipe_update =
        [=] (event<wlr_pointer_swipe_update_event> *ev)
    {
        if (!swiping)
        {
            return;
        }

        update_pan(ev->event->dx);
    };

    wf::signal::connection_t<event<wlr_pointer_swipe_end_event>> on_swipe_end =
        [=] (event<wlr_pointer_swipe_end_event> *ev)
    {
        if (!swiping)
        {
            return;
        }

        end_pan();
    };

    /* ---------------- pan control (shared by keys/swipe/IPC) ---------------- */

    bool begin_pan()
    {
        if (swiping || !output->activate_plugin(&grab_interface))
        {
            return false;
        }

        /* If a step animation is in flight, land it where it is now. */
        settle_animation();
        swiping    = true;
        drag_accum = 0.0;
        input_grab->grab_input(wf::scene::layer::OVERLAY);
        wf::get_core().seat->focus_output(output);
        return true;
    }

    void update_pan(double dx)
    {
        if (!swiping)
        {
            return;
        }

        drag_accum += dx;
        drag_px = (int)std::round(drag_accum);
        apply_offset();
    }

    void end_pan()
    {
        if (!swiping)
        {
            return;
        }

        swiping = false;
        /* Snap to the tile nearest to the current (clamped) position. */
        int from = current_content_x();
        auto W = output->get_screen_size().width;
        tile_index = std::clamp((int)std::round(-(double)from / W), 0, N_TILES - 1);
        drag_px    = 0;
        drag_accum = 0.0;
        animate_content_from(from, -tile_index * W);
        input_grab->ungrab_input();
        output->deactivate_plugin(&grab_interface);
        LOGI("proto-pan swipe end: i=", tile_index);
    }

    void cancel_pan()
    {
        if (!swiping)
        {
            return;
        }

        swiping    = false;
        drag_px    = 0;
        drag_accum = 0.0;
        apply_offset();
        input_grab->ungrab_input();
        output->deactivate_plugin(&grab_interface);
    }

    /* ------------------------------------------------------------------------ */

    void setup_pan_rail()
    {
        pan_node = std::make_shared<wf::scene::translation_node_t>(false);
        wf::scene::add_front(output->wset()->get_node(), pan_node);

        auto size = output->get_screen_size();
        const wf::color_t colors[N_TILES] = {
            {0.29, 0.06, 0.35, 1.0}, /* purple */
            {0.12, 0.35, 0.20, 1.0}, /* green  */
            {0.45, 0.20, 0.10, 1.0}, /* orange */
        };

        for (int i = 0; i < N_TILES; i++)
        {
            auto rect = wf::color_rect_view_t::create(
                wf::VIEW_ROLE_DESKTOP_ENVIRONMENT, output, wf::scene::layer::WORKSPACE);
            rect->set_color(colors[i]);
            rect->set_border(0);
            rect->set_geometry({i * size.width, 0, size.width, size.height});
            /* Re-parent the rendered node into the pan rail (tile-wset pattern). */
            wf::scene::readd_front(pan_node, rect->get_root_node());
            rects.push_back(rect);
        }

        LOGI("proto-pan rail: ", N_TILES, " tiles of ", size.width, "x", size.height,
            ", pan_node bbox=", pan_node->get_bounding_box());
    }

    /* Content displacement of the rail, clamped to the rail ends. */
    int current_content_x() const
    {
        auto W = output->get_screen_size().width;
        return std::clamp(drag_px - tile_index * W, -(N_TILES - 1) * W, 0);
    }

    /** Offset-change pattern from src/view/layer-shell/layer-shell.cpp:99-110:
     * damage the union of old/new bounding boxes (in output coordinates!),
     * then update with the GEOMETRY flag. */
    void set_offset_raw(int offset_x)
    {
        wf::region_t damage = pan_node->get_bounding_box();
        pan_node->set_offset({offset_x, 0});
        damage |= pan_node->get_bounding_box();
        wf::scene::damage_node(pan_node, damage);
        wf::scene::update(pan_node, wf::scene::update_flag::GEOMETRY);
    }

    void apply_offset()
    {
        set_offset_raw(current_content_x());
    }

    void step(int dir)
    {
        auto W = output->get_screen_size().width;
        int from = current_content_x();
        tile_index = std::clamp(tile_index + dir, 0, N_TILES - 1);
        drag_px    = 0;
        drag_accum = 0.0;
        animate_content_from(from, -tile_index * W);
        LOGI("proto-pan step: i=", tile_index, " target=", -tile_index * W);
    }

    wf::json_t get_state_json()
    {
        wf::json_t r;
        r["tile"]    = tile_index;
        r["tiles"]   = N_TILES;
        /* Logical (target) position vs actual node offset (mid-animation). */
        r["offset"]  = current_content_x();
        r["node_offset"] = pan_node ? pan_node->get_offset().x : 0;
        r["drag_px"] = drag_px;
        r["swiping"] = swiping;
        r["has_view"] = has_pan_view;
        return r;
    }

    wf::json_t ipc_step(wf::json_t data)
    {
        if (!data.has_member("dir"))
        {
            return wf::ipc::json_error("missing 'dir'");
        }

        int dir = data["dir"];
        if ((dir != -1) && (dir != 1))
        {
            return wf::ipc::json_error("dir must be -1 or 1");
        }

        step(dir);
        return get_state_json();
    }

    /** Simulate a swipe for automated tests: {"op":"begin"} / {"op":"dx","dx":N}
     * / {"op":"end"} — exercises the same code path as real gestures. */
    wf::json_t ipc_drag(wf::json_t data)
    {
        if (!data.has_member("op"))
        {
            return wf::ipc::json_error("missing 'op'");
        }

        std::string op = data["op"];
        if (op == "begin")
        {
            return begin_pan() ? get_state_json() :
                   wf::ipc::json_error("cannot begin (already swiping or plugin busy)");
        } else if (op == "dx")
        {
            if (!swiping)
            {
                return wf::ipc::json_error("not swiping (send op=begin first)");
            }

            if (!data.has_member("dx"))
            {
                return wf::ipc::json_error("missing 'dx'");
            }

            update_pan((double)data["dx"]);
            return get_state_json();
        } else if (op == "end")
        {
            end_pan();
            return get_state_json();
        }

        return wf::ipc::json_error("op must be begin|dx|end");
    }

    void teardown_pan_rail()
    {
        /* Release the adopted view back to the wset node before dying. */
        if (has_pan_view && pan_view)
        {
            wf::scene::readd_front(output->wset()->get_node(), pan_view->get_root_node());
            pan_view = nullptr;
            has_pan_view = false;
        }

        for (auto& rect : rects)
        {
            if (rect)
            {
                rect->close();
            }
        }

        rects.clear();
        if (pan_node)
        {
            wf::scene::remove_child(pan_node);
            pan_node = nullptr;
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<proto_pan_output_t>);
