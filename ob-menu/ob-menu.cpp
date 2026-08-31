/* SPDX-License-Identifier: GPL-2.0-only */
/* ob-menu — openbox menu.xml menu system (core-level plugin; per-output
 * runtime lives in output-menu.cpp). */
#include "output-menu.hpp"
#include "ob-menu-events.hpp"

#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/output.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/bindings-repository.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/debug.hpp>

#include <algorithm>
#include <map>

namespace obmenu
{
class ob_menu_output_t : public wf::per_output_plugin_instance_t
{
  public:
    void init() override
    {
        output->add_activator(key_root, &on_key_root);
        output->add_activator(key_client, &on_key_client);
        LOGI("ob-menu: key_root = ",
            wf::option_type::to_string(key_root.value()),
            ", key_client = ", wf::option_type::to_string(key_client.value()));

        /* xdg toplevels asking for a window menu (show_window_menu) */
        output->connect(&on_show_window_menu);
        /* titlebar right-click / menu button from ob-decor */
        output->connect(&on_menu_request);
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

        /* labwc: the client-send-to menu acts on the focused client */
        if (id == DYN_MENU_CLIENT_SEND_TO)
        {
            menu_runtime->set_focused_view(wf::toplevel_cast(
                wf::get_core().seat->get_active_view()));
        }

        menu_runtime->open_root(id, anchor);
    }

    void open_client_at_titlebar()
    {
        auto view = wf::toplevel_cast(
            wf::get_core().seat->get_active_view());
        if (!view)
        {
            LOGE("ob-menu: client-menu requested but no active toplevel view");
            return;
        }

        open_client_menu_for(view);
    }

    OutputMenu *get_runtime()
    {
        return menu_runtime.get();
    }

  private:
    void open_client_menu_for(wayfire_toplevel_view view)
    {
        if (!view)
        {
            return;
        }

        if (!menu_runtime)
        {
            menu_runtime = std::make_unique<OutputMenu>(output);
        }

        if (menu_runtime->is_open())
        {
            menu_runtime->close_all();
        }

        menu_runtime->open_client_menu(view);
    }

    void open_client_menu_at(wayfire_toplevel_view view, wf::pointf_t anchor)
    {
        if (!view)
        {
            return;
        }

        /* clamp into the workarea (rough: exact height depends on items) */
        auto wa = output->workarea->get_workarea();
        anchor.x = std::clamp(anchor.x, (double)wa.x,
            (double)(wa.x + wa.width - 32));
        anchor.y = std::clamp(anchor.y, (double)wa.y,
            (double)(wa.y + wa.height - 32));

        if (!menu_runtime)
        {
            menu_runtime = std::make_unique<OutputMenu>(output);
        }

        if (menu_runtime->is_open())
        {
            menu_runtime->close_all();
        }

        menu_runtime->open_client_menu(view, anchor);
    }

    /* xdg show_window_menu: surface-relative position → output-local */
    wf::signal::connection_t<wf::view_show_window_menu_signal>
    on_show_window_menu = [=] (wf::view_show_window_menu_signal *ev)
    {
        auto view = wf::toplevel_cast(ev->view);
        if (!view || (view->get_output() != output))
        {
            return;
        }

        auto g = view->get_geometry();
        open_client_menu_at(view, wf::pointf_t{
            (double)g.x + ev->relative_position.x,
            (double)g.y + ev->relative_position.y});
    };

    /* ob-decor titlebar right-click / menu button */
    wf::signal::connection_t<obmenu::ob_menu_request_signal>
    on_menu_request = [=] (obmenu::ob_menu_request_signal *ev)
    {
        if (!menu_runtime)
        {
            menu_runtime = std::make_unique<OutputMenu>(output);
        }

        if (menu_runtime->is_open())
        {
            menu_runtime->close_all();
        }

        menu_runtime->set_focused_view(
            wf::toplevel_cast(ev->client_view));
        menu_runtime->open_root(ev->menu_id, ev->anchor);
    };

  private:
    /* activator bindings (command-plugin style). add_key's keybinding
     * matching silently failed for alt-modified bindings in this
     * environment while the activator path works; activators are also
     * the canonical wayfire binding mechanism. */
    wf::option_wrapper_t<wf::activatorbinding_t> key_root{"ob-menu/key_root_menu"};
    wf::option_wrapper_t<wf::activatorbinding_t> key_client{"ob-menu/key_client_menu"};
    wf::activator_callback on_key_root = [=] (auto)
    {
        LOGI("ob-menu: key_root_menu triggered (root-menu)");
        open_menu_at("root-menu", output->get_cursor_position());
        return true;
    };
    wf::activator_callback on_key_client = [=] (auto)
    {
        LOGI("ob-menu: key_client_menu binding fired");
        open_client_at_titlebar();
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
            if ((op == "open-root") && inst)
            {
                /* labwc semantics: no explicit position = open at the
                 * cursor (same as the keybinding path) */
                wf::pointf_t anchor = output->get_cursor_position();
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
            else if ((op == "open-client") && inst)
            {
                inst->open_client_at_titlebar();
            }

            /* re-take the runtime AFTER open-root may have created it */
            auto *rt = inst ? inst->get_runtime() : nullptr;
            if ((op == "close") && rt)
            {
                rt->close_all();
            }
            else if ((op.rfind("nav:", 0) == 0) && rt)
            {
                rt->handle_nav_key(op.substr(4));
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
