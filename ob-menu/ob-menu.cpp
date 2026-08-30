/* SPDX-License-Identifier: GPL-2.0-only */
/* ob-menu — openbox menu.xml menu system (core-level plugin; per-output
 * runtime lives in output-menu.cpp). */
#include "output-menu.hpp"

#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/bindings-repository.hpp>
#include <wayfire/debug.hpp>

#include <map>

namespace obmenu
{
class ob_menu_output_t : public wf::per_output_plugin_instance_t
{
  public:
    void init() override
    {
        output->add_key(key_root, &on_key_root);
    }

    void fini() override
    {
        if (menu_runtime)
        {
            menu_runtime.reset();
        }
    }

    void open_menu_at(const std::string &id, wf::pointf_t anchor)
    {
        if (!menu_runtime)
        {
            menu_runtime = std::make_unique<OutputMenu>(output);
        }

        if (menu_runtime->is_open())
        {
            menu_runtime->close_all();
        }

        menu_runtime->open_root(id, anchor);
    }

    OutputMenu *get_runtime()
    {
        return menu_runtime.get();
    }

  private:
    wf::option_wrapper_t<wf::keybinding_t> key_root{"ob-menu/key_root_menu"};
    wf::key_callback on_key_root = [=] (auto)
    {
        open_menu_at("root-menu", output->get_cursor_position());
        return true;
    };
    std::unique_ptr<OutputMenu> menu_runtime;
};

class wayfire_ob_menu : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        auto &state = MenuState::get();
        wf::option_wrapper_t<std::string> menu_file{"ob-menu/menu_file"};
        wf::option_wrapper_t<std::string> font{"ob-menu/font"};
        wf::option_wrapper_t<std::string> font_mi{"ob-menu/font_menuitem"};
        wf::option_wrapper_t<std::string> font_mh{"ob-menu/font_menuheader"};
        state.init(menu_file, font, font_mi, font_mh);
        LOGI("ob-menu init: ", state.set.menus.size(), " menus parsed");

        outputs.init_output_tracking();
        register_ipc();
    }

    void fini() override
    {
        ipc_repo->unregister_method("ob-menu/debug");
        outputs.fini_output_tracking();
    }

  private:
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;

    struct outputs_t : public wf::per_output_tracker_mixin_t<ob_menu_output_t>
    {
        ob_menu_output_t *get(wf::output_t *output)
        {
            auto it = output_instance.find(output);
            return it == output_instance.end() ? nullptr : it->second.get();
        }
    };

    outputs_t outputs;

    void register_ipc()
    {
        ipc_repo->register_method("ob-menu/debug", [this] (wf::json_t data)
        {
            std::string op = data.has_member("op") ?
                (std::string)data["op"] : "get-state";
            wf::json_t r;
            auto output = wf::get_core().seat->get_active_output();
            auto *inst  = outputs.get(output);
            auto *rt    = inst ? inst->get_runtime() : nullptr;

            if ((op == "open-root") && inst)
            {
                wf::pointf_t anchor = {100, 100};
                if (data.has_member("x"))
                {
                    anchor.x = (double)(int)data["x"];
                }

                if (data.has_member("y"))
                {
                    anchor.y = (double)(int)data["y"];
                }

                inst->open_menu_at(data.has_member("menu") ?
                    (std::string)data["menu"] : "root-menu", anchor);
            }
            else if ((op == "close") && rt)
            {
                rt->close_all();
            }

            if (rt)
            {
                r["state"] = rt->get_state();
            }
            else
            {
                r["state"] = nullptr;
            }

            return r;
        });
    }
};
} // namespace obmenu

DECLARE_WAYFIRE_PLUGIN(obmenu::wayfire_ob_menu);
