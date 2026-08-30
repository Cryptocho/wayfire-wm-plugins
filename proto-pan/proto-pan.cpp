#include <memory>
#include <vector>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/compositor-view.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/unstable/translation-node.hpp>
#include <wayfire/debug.hpp>

/**
 * proto-pan: P0 prototype for the scrolling layout's core risk mechanism —
 * a plugin-owned scene sublayer whose content can be freely panned.
 *
 * S2: mount a translation node ("pan rail") under the wset node and fill it
 * with N dummy color rects, one per tile. Visual/interactive checks in
 * TEST.md; here we verify headlessly that mounting + rendering + bbox work.
 */
class proto_pan_output_t : public wf::per_output_plugin_instance_t
{
  public:
    void init() override
    {
        LOGI("proto-pan init on ", output->to_string());
        setup_pan_rail();
    }

    void fini() override
    {
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

    void teardown_pan_rail()
    {
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
