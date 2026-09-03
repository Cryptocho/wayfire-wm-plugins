/* SPDX-License-Identifier: GPL-2.0-only */
/* ob-theme — openbox theme provider plugin (non-rendering).
 *
 * Owns the ONE libobtheme theme instance:
 *   - initializes it from [ob-theme] options (idempotent first-wins, so
 *     a consumer that ran first keeps its theme — provider logs it);
 *   - publishes ob_theme_data_t on core for ob-decor / ob-menu /
 *     ob-switcher and any future consumer;
 *   - offers an in-place reload path: IPC "ob-theme/debug" op=reload
 *     calls ob_theme_reinit() (same struct reparsed) and emits
 *     ob_theme_reloaded_signal on core so consumers re-render.
 *
 * It renders nothing and never finishes the theme: the singleton lives
 * for the whole process (wayfire does not unload plugins at runtime,
 * and ob_theme_finish would null the shared rc.theme out from under
 * every consumer — the exact crash decor-destroy-test.sh guards).
 */
#include "ob-theme-data.hpp"

#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>

#include <initializer_list>
#include <string>

namespace obtheme
{
class wayfire_ob_theme : public wf::plugin_interface_t
{
  public:
    /* Providers of basic services load first (plugin.hpp: lower hint =
     * earlier init, before core/plugins order is consulted). */
    int get_order_hint() const override
    {
        return -100;
    }

    void init() override
    {
        /* Keep option values in members: ob_theme_config holds c_str
         * pointers, and option_wrapper_t::value() returns temporaries
         * (dangling-c_str class of bugs, see AGENTS.md P1 lessons). */
        theme_name_ = read_with_fallback("ob-theme/theme_name",
            {"ob-decor/theme"});
        font_         = opt_string("ob-theme/font");
        font_menuitem_    = opt_string("ob-theme/font_menuitem");
        font_menuheader_  = opt_string("ob-theme/font_menuheader");

        struct ob_theme_config cfg = make_cfg();
        struct theme *theme = nullptr;
        if (ob_theme_init(&theme, cfg) != 0)
        {
            LOGE("ob-theme: ob_theme_init failed");
            return;
        }

        auto data = std::make_unique<ob_theme_data_t>();
        data->theme = theme;
        data->epoch = epoch_;
        wf::get_core().store_data(std::move(data), DATA_NAME);

        register_ipc();
        LOGI("ob-theme: provider ready, theme='", theme_name_, "'");
    }

    void fini() override
    {
        ipc_repo->unregister_method("ob-theme/debug");
        /* Intentionally NOT ob_theme_finish(): process-lifetime singleton,
         * consumers keep borrowed pointers. The data object stays on core
         * for the same reason. */
    }

  private:
    static constexpr const char *DATA_NAME = "ob-theme";

    std::string theme_name_, font_, font_menuitem_, font_menuheader_;
    uint32_t epoch_ = 1;
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;

    static std::string opt_string(const char *name)
    {
        wf::option_wrapper_t<std::string> opt{name};
        return opt.value();
    }

    /* First non-empty value among @names, "" if none (built-in defaults).
     * Missing options (plugin metadata not loaded) are tolerated. */
    static std::string read_with_fallback(const char *first,
        std::initializer_list<const char*> rest = {})
    {
        try
        {
            auto v = opt_string(first);
            if (!v.empty())
            {
                return v;
            }
        } catch (const std::exception&)
        {
            /* option not registered */
        }

        for (auto *name : rest)
        {
            try
            {
                auto v = opt_string(name);
                if (!v.empty())
                {
                    LOGI("ob-theme: ", first, " empty, falling back to ",
                        name);
                    return v;
                }
            } catch (const std::exception&)
            {
                /* option not registered */
            }
        }

        return "";
    }

    struct ob_theme_config make_cfg()
    {
        struct ob_theme_config cfg = {};
        cfg.theme_name      = theme_name_.c_str();
        cfg.font            = font_.c_str();
        cfg.font_menuitem   = font_menuitem_.c_str();
        cfg.font_menuheader = font_menuheader_.c_str();
        return cfg;
    }

    static wf::json_t state_json()
    {
        wf::json_t r;
        auto data = wf::get_core().get_data<ob_theme_data_t>(DATA_NAME);
        r["has_theme"] = data && data->theme;
        r["theme_name"] = read_current_theme_name();
        r["epoch"] = data ? (int)data->epoch : 0;
        return r;
    }

    /* rc.theme_name is the shim string libobtheme actually parsed with
     * (may differ from [ob-theme] options if a consumer initialized
     * first with its own config). */
    static std::string read_current_theme_name()
    {
        struct theme *t = ob_theme_current();
        return t ? std::string(ob_theme_parsed_theme_name()) : std::string();
    }

    void register_ipc()
    {
        ipc_repo->register_method("ob-theme/debug",
            [this] (wf::json_t data) -> wf::json_t
        {
            std::string op = data.has_member("op") ?
                (std::string)data["op"] : "get-state";
            if (op == "reload")
            {
                reload();
            }

            return state_json();
        });
    }

    void reload()
    {
        theme_name_ = read_with_fallback("ob-theme/theme_name",
            {"ob-decor/theme"});
        font_            = opt_string("ob-theme/font");
        font_menuitem_   = opt_string("ob-theme/font_menuitem");
        font_menuheader_ = opt_string("ob-theme/font_menuheader");

        if (ob_theme_reinit(make_cfg()) != 0)
        {
            LOGE("ob-theme: reload requested but no theme initialized");
            return;
        }

        epoch_++;
        auto data = wf::get_core().get_data<ob_theme_data_t>(DATA_NAME);
        if (data)
        {
            data->epoch = epoch_;
        }

        ob_theme_reloaded_signal ev{epoch_};
        wf::get_core().emit(&ev);
        LOGI("ob-theme: reloaded in place, epoch=", epoch_);
    }
};
} // namespace obtheme

DECLARE_WAYFIRE_PLUGIN(obtheme::wayfire_ob_theme);
