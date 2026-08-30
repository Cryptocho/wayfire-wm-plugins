#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/debug.hpp>

/**
 * proto-pan: P0 prototype for the scrolling layout's core risk mechanism —
 * a plugin-owned scene sublayer whose content can be freely panned.
 *
 * S1: minimal per-output plugin, only logs init/fini to validate the
 * out-of-tree build + load + hot-reload pipeline.
 */
class proto_pan_output_t : public wf::per_output_plugin_instance_t
{
  public:
    void init() override
    {
        LOGI("proto-pan init on ", output->to_string());
    }

    void fini() override
    {
        LOGI("proto-pan fini on ", output->to_string());
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<proto_pan_output_t>);
