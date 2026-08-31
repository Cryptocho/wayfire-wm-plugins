/* SPDX-License-Identifier: GPL-2.0-only */
/* layout-manager — global layout mode switching: stacking (native floating)
 * <-> sway-style H/V tiling, plus per-view float.
 *
 * M-A: mode state machine + IPC + keybinding skeleton. Later milestones add
 * the tiling tree engine (derived from Wayfire plugins/tile, GPL-2.0-only),
 * harvest/restore migration, float management and divider drag. */
#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/output.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/bindings-repository.hpp>
#include <wayfire/debug.hpp>

#include <string>

namespace lm
{
enum class layout_mode_t
{
    STACKING,
    TILING,
};

static const char *mode_name(layout_mode_t m)
{
    return m == layout_mode_t::TILING ? "tiling" : "stacking";
}

static bool mode_from_string(const std::string &s, layout_mode_t &out)
{
    if (s == "tiling")
    {
        out = layout_mode_t::TILING;
        return true;
    }

    if (s == "stacking")
    {
        out = layout_mode_t::STACKING;
        return true;
    }

    return false;
}

class wayfire_layout_manager;

/* Per-output instance: bindings are registered per-output, but the mode they
 * control is global (a single shared layout mode across all outputs). */
class lm_output_t : public wf::per_output_plugin_instance_t
{
  public:
    wayfire_layout_manager *plugin = nullptr;

    void init() override
    {
        output->add_activator(key_toggle_mode, &on_toggle_mode);
        LOGI("layout-manager: bindings registered on ", output->to_string());
    }

    void fini() override
    {
        output->rem_binding(&on_toggle_mode);
    }

    /* Defined after wayfire_layout_manager is complete. */
    bool handle_toggle_mode();

  private:
    wf::option_wrapper_t<wf::activatorbinding_t> key_toggle_mode{
        "layout-manager/key_toggle_mode"};
    wf::activator_callback on_toggle_mode = [=] (auto)
    {
        return handle_toggle_mode();
    };
};

class wayfire_layout_manager : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        wf::option_wrapper_t<std::string> default_mode{
            "layout-manager/default_mode"};
        std::string dm = default_mode.value();
        if (!mode_from_string(dm, mode))
        {
            LOGE("layout-manager: invalid default_mode '", dm,
                "', falling back to stacking");
            mode = layout_mode_t::STACKING;
        }

        outputs.plugin = this;
        outputs.init_output_tracking();
        register_ipc();
        LOGI("layout-manager init: mode=", mode_name(mode));

        /* M-B: when default_mode is tiling, migrate existing views after
         * output tracking is set up. */
    }

    void fini() override
    {
        unregister_ipc();
        outputs.fini_output_tracking();
    }

    bool toggle_mode()
    {
        set_mode(mode == layout_mode_t::TILING ? layout_mode_t::STACKING :
            layout_mode_t::TILING);
        return true;
    }

    /* Mode transitions: M-B adds harvest (stacking -> tiling) and restore
     * (tiling -> stacking) across all workspace sets. */
    void set_mode(layout_mode_t m)
    {
        if (m == mode)
        {
            return;
        }

        mode = m;
        LOGI("layout-manager: mode -> ", mode_name(mode));
        /* M-B: migrate_to_tiling() / migrate_to_stacking() */
    }

    layout_mode_t get_mode() const
    {
        return mode;
    }

  private:
    layout_mode_t mode = layout_mode_t::STACKING;

    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;

    struct outputs_t : public wf::per_output_tracker_mixin_t<lm_output_t>
    {
        wayfire_layout_manager *plugin = nullptr;

        lm_output_t *get(wf::output_t *output)
        {
            auto it = output_instance.find(output);
            return it == output_instance.end() ? nullptr : it->second.get();
        }

      protected:
        void handle_new_output(wf::output_t *output) override
        {
            auto inst = std::make_unique<lm_output_t>();
            inst->output = output;
            inst->plugin = plugin;
            auto ptr = inst.get();
            output_instance[output] = std::move(inst);
            ptr->init();
        }
    };

    outputs_t outputs;

    void register_ipc()
    {
        ipc_repo->register_method("layout-manager/get-mode",
            [this] (wf::json_t)
        {
            wf::json_t r;
            r["mode"] = mode_name(mode);
            return r;
        });

        ipc_repo->register_method("layout-manager/set-mode",
            [this] (wf::json_t data)
        {
            wf::json_t r;
            if (!data.has_member("mode"))
            {
                r["error"] = std::string("missing 'mode' member"
                    " (" + std::string("tiling|stacking") + ")");
                return r;
            }

            layout_mode_t m;
            std::string s = (std::string)data["mode"];
            if (!mode_from_string(s, m))
            {
                r["error"] = "invalid mode '" + s + "' (tiling|stacking)";
                return r;
            }

            set_mode(m);
            r["mode"] = mode_name(mode);
            return r;
        });

        ipc_repo->register_method("layout-manager/toggle-mode",
            [this] (wf::json_t)
        {
            toggle_mode();
            wf::json_t r;
            r["mode"] = mode_name(mode);
            return r;
        });
    }

    void unregister_ipc()
    {
        ipc_repo->unregister_method("layout-manager/get-mode");
        ipc_repo->unregister_method("layout-manager/set-mode");
        ipc_repo->unregister_method("layout-manager/toggle-mode");
    }
};
} // namespace lm

bool lm::lm_output_t::handle_toggle_mode()
{
    return plugin->toggle_mode();
}

DECLARE_WAYFIRE_PLUGIN(lm::wayfire_layout_manager);
