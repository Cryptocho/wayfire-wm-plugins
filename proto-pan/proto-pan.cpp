#include <algorithm>
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
#include <wayfire/debug.hpp>

/**
 * proto-pan: P0 prototype for the scrolling layout's core risk mechanism —
 * a plugin-owned scene sublayer whose content can be freely panned.
 *
 * S3: keyboard step-pan (<super>F1/F2) with proper damage handling, adoption
 * of the first mapped toplevel into the rail, and IPC methods (pan/step,
 * pan/get-state) so pan state can be verified without input devices.
 *
 * Pan model: rail of N_TILES screens; state = tile index i + continuous drag
 * (unused in S3). offset_x = clamp(-(i*W + drag_px), -(N-1)*W, 0).
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

        ipc_repo->register_method("proto-pan/step", [this] (wf::json_t data)
        {
            return ipc_step(data);
        });
        ipc_repo->register_method("proto-pan/get-state", [this] (wf::json_t)
        {
            return get_state_json();
        });
    }

    void fini() override
    {
        ipc_repo->unregister_method("proto-pan/step");
        ipc_repo->unregister_method("proto-pan/get-state");
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

    /* Pan state: current tile index + continuous drag amount (px, S4). */
    int tile_index = 0;
    int drag_px = 0;

    /* First toplevel adopted into the rail (prototype handles exactly one). */
    bool has_pan_view = false;
    wayfire_toplevel_view pan_view;

    wf::option_wrapper_t<wf::keybinding_t> key_left{"proto-pan/key_left"};
    wf::option_wrapper_t<wf::keybinding_t> key_right{"proto-pan/key_right"};
    wf::key_callback on_step_left  = [=] (auto) { step(-1); return true; };
    wf::key_callback on_step_right = [=] (auto) { step(+1); return true; };

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

    /** Offset-change pattern from src/view/layer-shell/layer-shell.cpp:99-110:
     * damage the union of old/new bounding boxes (in output coordinates!),
     * then update with the GEOMETRY flag. */
    void apply_offset()
    {
        auto W = output->get_screen_size().width;
        int max_offset = (N_TILES - 1) * W;
        int offset_x   = std::clamp(-(tile_index * W + drag_px), -max_offset, 0);

        wf::region_t damage = pan_node->get_bounding_box();
        pan_node->set_offset({offset_x, 0});
        damage |= pan_node->get_bounding_box();
        wf::scene::damage_node(pan_node, damage);
        wf::scene::update(pan_node, wf::scene::update_flag::GEOMETRY);
    }

    void step(int dir)
    {
        tile_index = std::clamp(tile_index + dir, 0, N_TILES - 1);
        drag_px = 0;
        apply_offset();
        LOGI("proto-pan step: i=", tile_index, " offset=", pan_node->get_offset().x);
    }

    wf::json_t get_state_json()
    {
        wf::json_t r;
        r["tile"]    = tile_index;
        r["tiles"]   = N_TILES;
        r["offset"]  = pan_node ? pan_node->get_offset().x : 0;
        r["drag_px"] = drag_px;
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
