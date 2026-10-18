/*
 * ext-workspace — expose wayfire workspace state via the ext-workspace-v1
 * protocol (staging, version 1), as an out-of-tree plugin.
 *
 * Design (aligned with user 2026-09-04):
 *   - workspace group <-> workspace_set_t (1:1). A wayfire wset attaches to a
 *     single output at a time; a group follows the wset across output changes.
 *   - workspace <-> every (x, y) cell of the wset grid.
 *   - name: row-major, 1-based ("1".."N"); coordinates = [x, y] (0-based).
 *   - state: only the `active` bit is sent. `urgent` has no source in wayfire
 *     0.10 core, `hidden` does not fit the grid semantics — both omitted.
 *   - capabilities: workspaces advertise `activate` only; groups advertise
 *     nothing (no create_workspace). Unsupported requests are ignored, as
 *     the protocol allows.
 *   - `id` event is not sent (optional in the protocol; deferred).
 *   - activate request -> emit workspace_change_request_signal on the output
 *     (same hook vswitch listens to, so animating plugins may handle it);
 *     if nobody set carried_out, fall back to wset->set_workspace().
 *
 * Resource lifecycle: all resources created by this plugin are destroyed in
 * fini() before the plugin object dies (wayfire core unloads plugins before
 * wl_display_destroy_clients(), see src/core/core.cpp:fini()), so the
 * destroy callbacks always run while the plugin object is alive.
 */

#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/util.hpp>

#include "ext-workspace-v1-server-protocol.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ew
{
class ext_workspace_plugin_t;

/** Per-client protocol state. Lifetime: owned by the plugin's clients list. */
/** Ordering for the (wset index, coordinates) workspace handle key. */
struct ws_key_less
{
    bool operator()(const std::pair<uint64_t, wf::point_t> &a,
        const std::pair<uint64_t, wf::point_t> &b) const
    {
        if (a.first != b.first)
        {
            return a.first < b.first;
        }

        if (a.second.x != b.second.x)
        {
            return a.second.x < b.second.x;
        }

        return a.second.y < b.second.y;
    }
};

struct client_state_t
{
    ext_workspace_plugin_t *plugin = nullptr;
    wl_resource *manager = nullptr;
    bool stopped = false; // after a stop request, no more events except finished

    /** wset index -> group handle resource */
    std::map<uint64_t, wl_resource*> group_handles;
    /** (wset index, coordinates) -> workspace handle resource */
    std::map<std::pair<uint64_t, wf::point_t>, wl_resource*, ws_key_less> ws_handles;
    /** wset index -> outputs announced via output_enter (for symmetric leaves) */
    std::map<uint64_t, std::set<wlr_output*>> entered_outputs;

    void destroy_handles();
};

struct group_res_data_t
{
    client_state_t *cs;
    uint64_t wset_index;
};

struct ws_res_data_t
{
    client_state_t *cs;
    uint64_t wset_index;
    wf::point_t coords;
};

/* ------------------------------------------------------------------ */

static client_state_t *client_of(wl_resource *res)
{
    return static_cast<client_state_t*>(wl_resource_get_user_data(res));
}

/** Find the client's already-bound wl_output resource for a wlr_output, if any. */
static wl_resource *find_client_output_resource(client_state_t &cs, wlr_output *out)
{
    wl_resource *res;
    wl_resource_for_each(res, &out->resources)
    {
        if (wl_resource_get_client(res) == wl_resource_get_client(cs.manager))
        {
            return res;
        }
    }

    return nullptr;
}

/** Look up a workspace set by its stable index. */
static wf::workspace_set_t *find_wset(uint64_t index)
{
    for (auto wp : wf::workspace_set_t::get_all())
    {
        if (wp->get_index() == index)
        {
            return wp.get();
        }
    }

    return nullptr;
}

static std::string workspace_name(const wf::dimensions_t &grid, wf::point_t coords)
{
    return std::to_string(coords.y * grid.width + coords.x + 1);
}

/* ------------------------ protocol request handlers ----------------------- */

static void handle_manager_commit(wl_client *client, wl_resource *resource)
{
    /* All broadcasts are emitted immediately and atomically per batch
     * (reconcile sends one done at the end); nothing to buffer here. */
    LOGD("ext-workspace: [req] commit from client ", (void*)client);
}

static void handle_manager_stop(wl_client *client, wl_resource *resource)
{
    LOGD("ext-workspace: [req] stop from client ", (void*)client);
    auto *cs = client_of(resource);
    cs->stopped = true;
    ext_workspace_manager_v1_send_finished(resource);
    /* The compositor must destroy the object immediately after finished. */
    wl_resource_destroy(resource); // -> manager_destroyed -> client removed
}

static void handle_group_create_workspace(wl_client *client, wl_resource *resource,
    const char *workspace)
{
    /* Capability not advertised; per protocol the compositor may ignore. */
    LOGD("ext-workspace: ignoring create_workspace request");
}

static void handle_group_destroy(wl_client *client, wl_resource *resource)
{
    LOGD("ext-workspace: [req] group destroy from client ", (void*)client);
    wl_resource_destroy(resource);
}

static void handle_ws_destroy(wl_client *client, wl_resource *resource)
{
    LOGD("ext-workspace: [req] workspace destroy from client ", (void*)client);
    wl_resource_destroy(resource);
}

static void handle_ws_activate(wl_client *client, wl_resource *resource)
{
    auto *d  = static_cast<ws_res_data_t*>(wl_resource_get_user_data(resource));
    LOGD("ext-workspace: [req] activate workspace ", d->coords.x, ",", d->coords.y,
        " (wset ", d->wset_index, ") from client ", (void*)client);
    auto *wset = find_wset(d->wset_index);
    if (!wset || !wset->get_attached_output())
    {
        LOGD("ext-workspace: activate on a detached workspace set, ignoring");
        return;
    }

    if (!wset->is_workspace_valid(d->coords))
    {
        LOGD("ext-workspace: activate on invalid workspace ", d->coords);
        return;
    }

    auto *output = wset->get_attached_output();
    wf::workspace_change_request_signal ev;
    ev.old_viewport = wset->get_current_workspace();
    ev.new_viewport = d->coords;
    ev.output       = output;
    LOGD("ext-workspace: emitting workspace_change_request_signal ", ev.old_viewport,
        " -> ", ev.new_viewport, " on ", output->to_string());
    output->emit(&ev);

    if (ev.carried_out)
    {
        LOGD("ext-workspace: request carried out by another plugin");
    } else
    {
        /* No plugin handled the request (e.g. no vswitch loaded): do it directly. */
        LOGD("ext-workspace: nobody carried out the request, using set_workspace");
        wset->set_workspace(d->coords);
    }
}

static void handle_ws_deactivate(wl_client *client, wl_resource *resource)
{
    LOGD("ext-workspace: [req] deactivate (not advertised) from client ", (void*)client);
    /* capability not advertised; ignore */
}

static void handle_ws_assign(wl_client *client, wl_resource *resource,
    wl_resource *workspace_group)
{
    LOGD("ext-workspace: [req] assign (not advertised) from client ", (void*)client);
    /* capability not advertised; ignore */
}

static void handle_ws_remove(wl_client *client, wl_resource *resource)
{
    LOGD("ext-workspace: [req] remove (not advertised) from client ", (void*)client);
    /* capability not advertised; ignore */
}

static const struct ext_workspace_manager_v1_interface manager_impl = {
    .commit = handle_manager_commit,
    .stop   = handle_manager_stop,
};

static const struct ext_workspace_group_handle_v1_interface group_impl = {
    .create_workspace = handle_group_create_workspace,
    .destroy = handle_group_destroy,
};

static const struct ext_workspace_handle_v1_interface ws_impl = {
    .destroy    = handle_ws_destroy,
    .activate   = handle_ws_activate,
    .deactivate = handle_ws_deactivate,
    .assign     = handle_ws_assign,
    .remove     = handle_ws_remove,
};

/* --------------------------- resource destruction ------------------------- */

static void group_handle_destroyed(wl_resource *resource)
{
    auto *d = static_cast<group_res_data_t*>(wl_resource_get_user_data(resource));
    /* The owning client_state_t outlives all its handle resources: it is
     * removed (destroying its handles) only from the plugin or from the
     * manager destructor. */
    d->cs->group_handles.erase(d->wset_index);
    delete d;
}

static void ws_handle_destroyed(wl_resource *resource)
{
    auto *d = static_cast<ws_res_data_t*>(wl_resource_get_user_data(resource));
    d->cs->ws_handles.erase({d->wset_index, d->coords});
    delete d;
}

void client_state_t::destroy_handles()
{
    std::vector<wl_resource*> all;
    for (auto& [_, g] : group_handles)
    {
        all.push_back(g);
    }

    for (auto& [_, w] : ws_handles)
    {
        all.push_back(w);
    }

    group_handles.clear();
    ws_handles.clear();
    entered_outputs.clear();
    for (auto *r : all)
    {
        wl_resource_destroy(r); // runs the destroy callbacks, which clean maps
    }
}

/* ------------------------------ the plugin -------------------------------- */

class ext_workspace_plugin_t : public wf::plugin_interface_t
{
  public:
    void init() override;
    void fini() override;
    bool is_unloadable() override
    {
        return false;
    }

    void remove_client(client_state_t *cs)
    {
        /* Destroy the handle resources first: their destroy callbacks touch
         * the client_state, which must stay alive until after teardown. */
        cs->destroy_handles();
        clients.erase(std::find_if(clients.begin(), clients.end(),
            [&] (const auto &p) { return p.get() == cs; }));
        LOGD("ext-workspace: client removed, ", clients.size(), " client(s) remain");
    }

    /** wl_resource destructor for the manager: tears down the whole client. */
    static void manager_destroyed(wl_resource *resource)
    {
        auto *cs = client_of(resource);
        cs->plugin->remove_client(cs);
    }

  private:
    friend struct client_state_t;

    wl_global *global = nullptr;
    std::vector<std::unique_ptr<client_state_t>> clients;

    struct output_conns_t
    {
        std::unique_ptr<wf::signal::connection_t<wf::workspace_changed_signal>> ws_changed;
        std::unique_ptr<wf::signal::connection_t<wf::workspace_set_changed_signal>> wset_changed;
    };

    std::map<wf::output_t*, output_conns_t> output_conns;

    /* Delayed retries for output_enter when the client has not yet bound the
     * new wl_output (hotplug: the wl_output global may reach the client a bit
     * later than our output_added event). */
    int pending_rounds = 0;
    wf::wl_timer<false> retry_timer;

    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;

    /* ---------------------- global binding & full state ------------------- */

    static void bind_manager(wl_client *client, void *data, uint32_t version, uint32_t id)
    {
        auto *plugin = static_cast<ext_workspace_plugin_t*>(data);
        auto *resource = wl_resource_create(client, &ext_workspace_manager_v1_interface,
            std::min(version, 1u), id);
        if (!resource)
        {
            wl_client_post_no_memory(client);
            return;
        }

        auto cs = std::make_unique<client_state_t>();
        cs->plugin  = plugin;
        cs->manager = resource;
        wl_resource_set_user_data(resource, cs.get());
        wl_resource_set_implementation(resource, &manager_impl, cs.get(), manager_destroyed);
        plugin->clients.push_back(std::move(cs));
        LOGD("ext-workspace: client bound ext_workspace_manager_v1");
        plugin->reconcile_all();
        /* The client may bind its wl_outputs a moment after the manager;
         * retry output_enter until all attached outputs are announced. */
        plugin->pending_rounds = 0;
        plugin->schedule_pending_reconcile();
    }

    /* ----------------------------- reconciling ---------------------------- */

    void reconcile_all()
    {
        for (auto &cs : clients)
        {
            reconcile(*cs);
        }
    }

    void reconcile(client_state_t &cs)
    {
        if (cs.stopped || !cs.manager)
        {
            return;
        }

        std::set<uint64_t> live_indices;
        for (auto wp : wf::workspace_set_t::get_all())
        {
            auto *wset = wp.get();
            uint64_t idx = wset->get_index();
            live_indices.insert(idx);

            wl_resource *group = get_or_create_group(cs, idx);
            if (!group)
            {
                continue;
            }

            reconcile_group_outputs(cs, idx, wset, group);
            reconcile_group_workspaces(cs, idx, wset, group);
        }

        /* Remove groups whose wset is gone. */
        std::vector<uint64_t> dead;
        for (auto& [idx, _] : cs.group_handles)
        {
            if (!live_indices.count(idx))
            {
                dead.push_back(idx);
            }
        }

        for (uint64_t idx : dead)
        {
            remove_group(cs, idx);
        }

        ext_workspace_manager_v1_send_done(cs.manager);
    }

    wl_resource *get_or_create_group(client_state_t &cs, uint64_t idx)
    {
        auto it = cs.group_handles.find(idx);
        if (it != cs.group_handles.end())
        {
            return it->second;
        }

        wl_resource *group = wl_resource_create(wl_resource_get_client(cs.manager),
            &ext_workspace_group_handle_v1_interface, 1, 0);
        if (!group)
        {
            LOGE("ext-workspace: out of memory creating group handle");
            return nullptr;
        }

        auto *d = new group_res_data_t{&cs, idx};
        wl_resource_set_user_data(group, d);
        wl_resource_set_implementation(group, &group_impl, d, group_handle_destroyed);
        cs.group_handles[idx] = group;

        ext_workspace_manager_v1_send_workspace_group(cs.manager, group);
        ext_workspace_group_handle_v1_send_capabilities(group, 0);
        return group;
    }

    void reconcile_group_outputs(client_state_t &cs, uint64_t idx,
        wf::workspace_set_t *wset, wl_resource *group)
    {
        auto &entered = cs.entered_outputs[idx];
        std::set<wlr_output*> current;
        if (auto *out = wset->get_attached_output())
        {
            current.insert(out->handle);
        }

        for (auto *wo : entered)
        {
            if (current.count(wo))
            {
                continue;
            }

            if (auto *ores = find_client_output_resource(cs, wo))
            {
                ext_workspace_group_handle_v1_send_output_leave(group, ores);
            }
        }

        /* Rebuild the entered set: keep still-current ones, add newly entered. */
        std::set<wlr_output*> next;
        for (auto *wo : current)
        {
            if (entered.count(wo))
            {
                next.insert(wo);
                continue;
            }

            if (auto *ores = find_client_output_resource(cs, wo))
            {
                ext_workspace_group_handle_v1_send_output_enter(group, ores);
                next.insert(wo);
            } else
            {
                LOGD("ext-workspace: client has not bound wl_output for output yet");
            }
        }

        entered = next;
    }

    void reconcile_group_workspaces(client_state_t &cs, uint64_t idx,
        wf::workspace_set_t *wset, wl_resource *group)
    {
        auto grid  = wset->get_workspace_grid_size();
        auto &map  = cs.ws_handles;
        std::vector<std::pair<uint64_t, wf::point_t>> stale;

        for (auto& [key, res] : map)
        {
            if (key.first != idx)
            {
                continue;
            }

            if ((key.second.x >= grid.width) || (key.second.y >= grid.height))
            {
                stale.push_back(key);
            }
        }

        for (auto &key : stale)
        {
            remove_workspace(cs, idx, key.second);
        }

        for (int y = 0; y < grid.height; y++)
        {
            for (int x = 0; x < grid.width; x++)
            {
                wf::point_t coords{x, y};
                wl_resource *ws = get_or_create_workspace(cs, idx, coords, group);
                if (!ws)
                {
                    continue;
                }

                /* Attributes are resent on every reconcile: idempotent for the
                 * client, and automatically covers renames / active changes. */
                auto name = workspace_name(grid, coords);
                ext_workspace_handle_v1_send_name(ws, name.c_str());

                wl_array arr;
                wl_array_init(&arr);
                auto *xy = static_cast<uint32_t*>(wl_array_add(&arr, 2 * sizeof(uint32_t)));
                xy[0] = coords.x;
                xy[1] = coords.y;
                ext_workspace_handle_v1_send_coordinates(ws, &arr);
                wl_array_release(&arr);

                uint32_t state = (wset->get_current_workspace() == coords) ?
                    EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE : 0;
                ext_workspace_handle_v1_send_state(ws, state);
                ext_workspace_handle_v1_send_capabilities(ws,
                    EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE);
            }
        }
    }

    wl_resource *get_or_create_workspace(client_state_t &cs, uint64_t idx,
        wf::point_t coords, wl_resource *group)
    {
        auto it = cs.ws_handles.find({idx, coords});
        if (it != cs.ws_handles.end())
        {
            return it->second;
        }

        wl_resource *ws = wl_resource_create(wl_resource_get_client(cs.manager),
            &ext_workspace_handle_v1_interface, 1, 0);
        if (!ws)
        {
            LOGE("ext-workspace: out of memory creating workspace handle");
            return nullptr;
        }

        auto *d = new ws_res_data_t{&cs, idx, coords};
        wl_resource_set_user_data(ws, d);
        wl_resource_set_implementation(ws, &ws_impl, d, ws_handle_destroyed);
        cs.ws_handles[{idx, coords}] = ws;

        ext_workspace_manager_v1_send_workspace(cs.manager, ws);
        ext_workspace_group_handle_v1_send_workspace_enter(group, ws);
        return ws;
    }

    /** Protocol removal dance: leave from its group (if any), removed, destroy. */
    void remove_workspace(client_state_t &cs, uint64_t idx, wf::point_t coords)
    {
        auto key = std::make_pair(idx, coords);
        auto it  = cs.ws_handles.find(key);
        if (it == cs.ws_handles.end())
        {
            return;
        }

        wl_resource *ws = it->second;
        auto git = cs.group_handles.find(idx);
        if (git != cs.group_handles.end())
        {
            ext_workspace_group_handle_v1_send_workspace_leave(git->second, ws);
        }

        ext_workspace_handle_v1_send_removed(ws);
        /* destroy -> ws_handle_destroyed -> erased from the map */
        wl_resource_destroy(ws);
    }

    void remove_group(client_state_t &cs, uint64_t idx)
    {
        auto git = cs.group_handles.find(idx);
        if (git == cs.group_handles.end())
        {
            return;
        }

        /* All members must leave before the group is removed. */
        std::vector<wf::point_t> members;
        for (auto& [key, _] : cs.ws_handles)
        {
            if (key.first == idx)
            {
                members.push_back(key.second);
            }
        }

        for (auto &coords : members)
        {
            remove_workspace(cs, idx, coords);
        }

        ext_workspace_group_handle_v1_send_removed(git->second);
        wl_resource_destroy(git->second);
        cs.entered_outputs.erase(idx);
    }

    /* ------------------------------ signals ------------------------------- */

    void attach_output(wf::output_t *output)
    {
        auto &c = output_conns[output];
        c.ws_changed = std::make_unique<wf::signal::connection_t<wf::workspace_changed_signal>>(
            [this] (auto) { reconcile_all(); });
        output->connect(c.ws_changed.get());
        c.wset_changed = std::make_unique<wf::signal::connection_t<wf::workspace_set_changed_signal>>(
            [this] (auto) { reconcile_all(); });
        output->connect(c.wset_changed.get());
    }

    /** True when some client has not yet bound a wl_output of an attached group output. */
    bool has_missing_outputs()
    {
        for (auto &cs : clients)
        {
            if (cs->stopped)
            {
                continue;
            }

            for (auto wp : wf::workspace_set_t::get_all())
            {
                auto *wset  = wp.get();
                auto  group = cs->group_handles.find(wset->get_index());
                if (group == cs->group_handles.end())
                {
                    continue;
                }

                auto *out = wset->get_attached_output();
                if (!out || !cs->entered_outputs[wset->get_index()].count(out->handle))
                {
                    return true;
                }
            }
        }

        return false;
    }

    void schedule_pending_reconcile()
    {
        if (pending_rounds >= 10)
        {
            return;
        }

        pending_rounds++;
        retry_timer.set_timeout(100, [this] ()
        {
            reconcile_all();
            if (has_missing_outputs())
            {
                schedule_pending_reconcile();
            } else
            {
                pending_rounds = 0;
            }
        });
    }

    wf::signal::connection_t<wf::output_added_signal> on_output_added =
        [=] (wf::output_added_signal *ev)
    {
        attach_output(ev->output);
        pending_rounds = 0;
        schedule_pending_reconcile();
    };

    wf::signal::connection_t<wf::output_removed_signal> on_output_removed =
        [=] (wf::output_removed_signal *ev)
    {
        output_conns.erase(ev->output);
        reconcile_all();
    };

    wf::signal::connection_t<wf::reload_config_signal> on_reload_config =
        [=] (wf::reload_config_signal *)
    {
        /* Grid size may have changed via core/{vwidth,vheight}. */
        reconcile_all();
    };

    /* -------------------------------- IPC --------------------------------- */

    wf::json_t get_state_json()
    {
        wf::json_t groups = wf::json_t::array();
        for (auto wp : wf::workspace_set_t::get_all())
        {
            auto *wset = wp.get();
            wf::json_t g;
            g["index"] = wset->get_index();
            auto *out  = wset->get_attached_output();
            g["output"] = std::string(out ? out->to_string() : "(none)");

            auto grid = wset->get_workspace_grid_size();
            auto cur  = wset->get_current_workspace();
            wf::json_t wss = wf::json_t::array();
            for (int y = 0; y < grid.height; y++)
            {
                for (int x = 0; x < grid.width; x++)
                {
                    wf::json_t w;
                    w["name"]   = workspace_name(grid, {x, y});
                    w["x"] = x;
                    w["y"] = y;
                    w["active"] = (cur == wf::point_t{x, y});
                    wss.append(w);
                }
            }

            g["workspaces"] = wss;
            groups.append(g);
        }

        wf::json_t r;
        r["groups"]  = groups;
        r["clients"] = (int)clients.size();
        return r;
    }
};

void ext_workspace_plugin_t::init()
{
    global = wl_global_create(wf::get_core().display,
        &ext_workspace_manager_v1_interface, 1, this, bind_manager);
    if (!global)
    {
        LOGE("ext-workspace: failed to register the wl_global");
        return;
    }

    for (auto *output : wf::get_core().output_layout->get_outputs())
    {
        attach_output(output);
    }

    wf::get_core().output_layout->connect(&on_output_added);
    wf::get_core().output_layout->connect(&on_output_removed);
    wf::get_core().connect(&on_reload_config);

    ipc_repo->register_method("ext-workspace/get-state", [this] (wf::json_t)
    {
        return get_state_json();
    });

    LOGI("ext-workspace: ext_workspace_manager_v1 global registered");
}

void ext_workspace_plugin_t::fini()
{
    ipc_repo->unregister_method("ext-workspace/get-state");
    retry_timer.disconnect();

    /* Destroy every client state while the plugin object is still alive:
     * the destroy callbacks reference it. After this, wayfire's shutdown
     * sequence (wl_display_destroy_clients) cannot touch our callbacks. */
    while (!clients.empty())
    {
        remove_client(clients.front().get());
    }

    if (global)
    {
        wl_global_destroy(global);
        global = nullptr;
    }

    LOGI("ext-workspace: fini");
}
} // namespace ew

DECLARE_WAYFIRE_PLUGIN(ew::ext_workspace_plugin_t);