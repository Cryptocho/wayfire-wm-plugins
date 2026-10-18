/*
 * ext-workspace-client — minimal ext-workspace-v1 test client.
 *
 * Binds ext_workspace_manager_v1 (and wl_output, so the compositor announces
 * output membership), replays the initial state, then optionally activates a
 * workspace and asserts the active transition, and optionally stops and
 * expects the finished event. Exits 0 iff all requested assertions hold.
 *
 * Usage: ext-workspace-client [--expect-groups N] [--expect-workspaces N]
 *   [--expect-outputs N] [--expect-active X,Y]
 *   [--activate X,Y --then-expect-active X,Y]
 *   [--stop] WAYLAND_DISPLAY
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-client.h>
#include "ext-workspace-v1-client-protocol.h"

#define MAX_GROUPS 16
#define MAX_WS 256

struct group {
    struct ext_workspace_group_handle_v1 *handle;
    int removed;
    int n_outputs;
    uint32_t caps;
    int n_ws_entered;
};

struct workspace {
    struct ext_workspace_handle_v1 *handle;
    char name[64];
    uint32_t coords[8];
    int n_coords;
    uint32_t state;
    uint32_t caps;
    int removed;
};

static struct {
    struct wl_display *display;
    struct wl_registry *registry;
    struct ext_workspace_manager_v1 *manager;
    struct group groups[MAX_GROUPS];
    int n_groups;
    struct workspace workspaces[MAX_WS];
    int n_ws;
    int done_count;
    int finished;
} g;


/* ------------------------------ listeners -------------------------------- */

static void group_handle_capabilities(void *data,
    struct ext_workspace_group_handle_v1 *handle, uint32_t caps)
{
    for (int i = 0; i < g.n_groups; i++)
    {
        if (g.groups[i].handle == handle)
        {
            g.groups[i].caps = caps;
        }
    }
}

static void group_handle_output_enter(void *data,
    struct ext_workspace_group_handle_v1 *handle, struct wl_output *output)
{
    for (int i = 0; i < g.n_groups; i++)
    {
        if (g.groups[i].handle == handle)
        {
            g.groups[i].n_outputs++;
            printf("[client] group %p output_enter\n", (void*)handle);
        }
    }
}

static void group_handle_output_leave(void *data,
    struct ext_workspace_group_handle_v1 *handle, struct wl_output *output)
{
    for (int i = 0; i < g.n_groups; i++)
    {
        if (g.groups[i].handle == handle)
        {
            g.groups[i].n_outputs--;
        }
    }
}

static void group_handle_workspace_enter(void *data,
    struct ext_workspace_group_handle_v1 *handle,
    struct ext_workspace_handle_v1 *workspace)
{
    for (int i = 0; i < g.n_groups; i++)
    {
        if (g.groups[i].handle == handle)
        {
            g.groups[i].n_ws_entered++;
        }
    }
}

static void group_handle_workspace_leave(void *data,
    struct ext_workspace_group_handle_v1 *handle,
    struct ext_workspace_handle_v1 *workspace)
{
    for (int i = 0; i < g.n_groups; i++)
    {
        if (g.groups[i].handle == handle)
        {
            g.groups[i].n_ws_entered--;
        }
    }
}

static void group_handle_removed(void *data,
    struct ext_workspace_group_handle_v1 *handle)
{
    for (int i = 0; i < g.n_groups; i++)
    {
        if (g.groups[i].handle == handle)
        {
            g.groups[i].removed = 1;
        }
    }
}

static const struct ext_workspace_group_handle_v1_listener group_listener = {
    .capabilities    = group_handle_capabilities,
    .output_enter    = group_handle_output_enter,
    .output_leave    = group_handle_output_leave,
    .workspace_enter = group_handle_workspace_enter,
    .workspace_leave = group_handle_workspace_leave,
    .removed         = group_handle_removed,
};

static void ws_handle_id(void *data, struct ext_workspace_handle_v1 *handle,
    const char *id)
{ /* not advertised by wayfire; ignore */ }

static void ws_handle_name(void *data, struct ext_workspace_handle_v1 *handle,
    const char *name)
{
    struct workspace *w = ext_workspace_handle_v1_get_user_data(handle);
    if (w)
    {
        snprintf(w->name, sizeof(w->name), "%s", name);
    }
}

static void ws_handle_coordinates(void *data,
    struct ext_workspace_handle_v1 *handle, struct wl_array *coordinates)
{
    struct workspace *w = ext_workspace_handle_v1_get_user_data(handle);
    if (!w)
    {
        return;
    }

    w->n_coords = 0;
    const uint32_t *p = coordinates->data;
    for (size_t i = 0; i < coordinates->size / sizeof(uint32_t); i++)
    {
        if (w->n_coords < 8)
        {
            w->coords[w->n_coords++] = p[i];
        }
    }
}

static void ws_handle_state(void *data, struct ext_workspace_handle_v1 *handle,
    uint32_t state)
{
    struct workspace *w = ext_workspace_handle_v1_get_user_data(handle);
    if (w)
    {
        w->state = state;
    }
}

static void ws_handle_capabilities(void *data,
    struct ext_workspace_handle_v1 *handle, uint32_t caps)
{
    struct workspace *w = ext_workspace_handle_v1_get_user_data(handle);
    if (w)
    {
        w->caps = caps;
    }
}

static void ws_handle_removed(void *data, struct ext_workspace_handle_v1 *handle)
{
    struct workspace *w = ext_workspace_handle_v1_get_user_data(handle);
    if (w)
    {
        w->removed = 1;
    }
}

static const struct ext_workspace_handle_v1_listener ws_listener = {
    .id          = ws_handle_id,
    .name        = ws_handle_name,
    .coordinates = ws_handle_coordinates,
    .state       = ws_handle_state,
    .capabilities = ws_handle_capabilities,
    .removed     = ws_handle_removed,
};

static void manager_workspace_group(void *data,
    struct ext_workspace_manager_v1 *manager,
    struct ext_workspace_group_handle_v1 *group)
{
    if (g.n_groups >= MAX_GROUPS)
    {
        return;
    }

    struct group *grp = &g.groups[g.n_groups++];
    memset(grp, 0, sizeof(*grp));
    grp->handle = group;
    ext_workspace_group_handle_v1_set_user_data(group, grp);
    ext_workspace_group_handle_v1_add_listener(group, &group_listener, NULL);
    printf("[client] + group\n");
}

static void manager_workspace(void *data, struct ext_workspace_manager_v1 *manager,
    struct ext_workspace_handle_v1 *workspace)
{
    if (g.n_ws >= MAX_WS)
    {
        return;
    }

    struct workspace *w = &g.workspaces[g.n_ws++];
    memset(w, 0, sizeof(*w));
    w->handle = workspace;
    int rc = ext_workspace_handle_v1_add_listener(workspace, &ws_listener, NULL);
    /* wl_proxy_add_listener overwrites user_data with its data argument:
     * set user_data AFTER adding the listener. */
    ext_workspace_handle_v1_set_user_data(workspace, w);
    (void)rc;
}

static void manager_done(void *data, struct ext_workspace_manager_v1 *manager)
{
    g.done_count++;
    printf("[client] done #%d\n", g.done_count);
}

static void manager_finished(void *data, struct ext_workspace_manager_v1 *manager)
{
    g.finished = 1;
    printf("[client] finished\n");
}

static const struct ext_workspace_manager_v1_listener manager_listener = {
    .workspace_group = manager_workspace_group,
    .workspace       = manager_workspace,
    .done            = manager_done,
    .finished        = manager_finished,
};

/* Keep a wl_output reference (does nothing) so the compositor announces it. */
static void output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y,
    int32_t pw, int32_t ph, int32_t subpixel, const char *make, const char *model,
    int32_t transform)
{}

static void output_mode(void *data, struct wl_output *output, uint32_t flags,
    int32_t w, int32_t h, int32_t refresh)
{}

static void output_done(void *data, struct wl_output *output)
{}

static void output_scale(void *data, struct wl_output *output, int32_t factor)
{}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode     = output_mode,
    .done     = output_done,
    .scale    = output_scale,
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
    const char *interface, uint32_t version)
{
    if (strcmp(interface, wl_output_interface.name) == 0)
    {
        struct wl_output *out = wl_registry_bind(registry, name,
            &wl_output_interface, 2);
        if (out)
        {
            wl_output_add_listener(out, &output_listener, NULL);
        }
    } else if (strcmp(interface, ext_workspace_manager_v1_interface.name) == 0)
    {
        g.manager = wl_registry_bind(registry, name,
            &ext_workspace_manager_v1_interface, 1);
        ext_workspace_manager_v1_add_listener(g.manager, &manager_listener, NULL);
        printf("[client] bound ext_workspace_manager_v1\n");
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_global_remove,
};

/* -------------------------------- helpers --------------------------------- */

static struct workspace *find_active(void)
{
    for (int i = 0; i < g.n_ws; i++)
    {
        if ((g.workspaces[i].state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE) &&
            !g.workspaces[i].removed)
        {
            return &g.workspaces[i];
        }
    }

    return NULL;
}

static struct workspace *find_by_coords(uint32_t x, uint32_t y)
{
    for (int i = 0; i < g.n_ws; i++)
    {
        if (!g.workspaces[i].removed && (g.workspaces[i].n_coords >= 2) &&
            (g.workspaces[i].coords[0] == x) && (g.workspaces[i].coords[1] == y))
        {
            return &g.workspaces[i];
        }
    }

    return NULL;
}

#define EXPECT(cond, ...) \
    do { \
        if (!(cond)) \
        { \
            printf("FAIL: "); \
            printf(__VA_ARGS__); \
            printf("\n"); \
            exit(1); \
        } \
    } while (0)

int main(int argc, char **argv)
{
    const char *display_name = NULL;
    int expect_groups = -1, expect_workspaces = -1, expect_outputs = -1;
    uint32_t expect_ax = 0, expect_ay = 0;
    int has_expect_active = 0;
    uint32_t activate_x = 0, activate_y = 0, then_x = 0, then_y = 0;
    int do_activate = 0, do_stop = 0;

    for (int i = 1; i < argc; i++)
    {
        const char *a = argv[i];
        if (!strcmp(a, "--expect-groups") && (i + 1 < argc))
        {
            expect_groups = atoi(argv[++i]);
        } else if (!strcmp(a, "--expect-workspaces") && (i + 1 < argc))
        {
            expect_workspaces = atoi(argv[++i]);
        } else if (!strcmp(a, "--expect-outputs") && (i + 1 < argc))
        {
            expect_outputs = atoi(argv[++i]);
        } else if (!strcmp(a, "--expect-active") && (i + 2 < argc))
        {
            expect_ax = atoi(argv[++i]);
            expect_ay = atoi(argv[++i]);
            has_expect_active = 1;
        } else if (!strcmp(a, "--activate") && (i + 2 < argc))
        {
            activate_x = atoi(argv[++i]);
            activate_y = atoi(argv[++i]);
            do_activate = 1;
        } else if (!strcmp(a, "--then-expect-active") && (i + 2 < argc))
        {
            then_x = atoi(argv[++i]);
            then_y = atoi(argv[++i]);
        } else if (!strcmp(a, "--stop"))
        {
            do_stop = 1;
        } else if (a[0] != '-')
        {
            display_name = a;
        } else
        {
            printf("unknown arg: %s\n", a);
            return 2;
        }
    }

    g.display = wl_display_connect(display_name);
    EXPECT(g.display, "cannot connect to wayland display");
    g.registry = wl_display_get_registry(g.display);
    wl_registry_add_listener(g.registry, &registry_listener, NULL);
    wl_display_roundtrip(g.display); // globals: wl_output + ext_workspace_manager
    EXPECT(g.manager, "ext_workspace_manager_v1 not offered");
    wl_display_roundtrip(g.display); // initial state
    wl_display_roundtrip(g.display);

    EXPECT(g.done_count >= 1, "no done event received");
    printf("[client] groups=%d workspaces=%d\n", g.n_groups, g.n_ws);

    if (expect_groups >= 0)
    {
        EXPECT(g.n_groups == expect_groups, "expected %d groups, got %d",
            expect_groups, g.n_groups);
    }

    if (expect_workspaces >= 0)
    {
        EXPECT(g.n_ws == expect_workspaces, "expected %d workspaces, got %d",
            expect_workspaces, g.n_ws);
    }

    if (expect_outputs >= 0)
    {
        /* Give the compositor's delayed output_enter retries a chance. */
        usleep(300000);
        wl_display_roundtrip(g.display);
        wl_display_roundtrip(g.display);
        int outs = g.n_groups ? g.groups[0].n_outputs : 0;
        EXPECT(outs == expect_outputs, "expected %d output_enter, got %d",
            expect_outputs, outs);
    }

    if (has_expect_active)
    {
        struct workspace *a = find_active();
        EXPECT(a, "no active workspace advertised");
        EXPECT((a->coords[0] == expect_ax) && (a->coords[1] == expect_ay),
            "active workspace is (%u,%u), expected (%u,%u)",
            a->coords[0], a->coords[1], expect_ax, expect_ay);
        EXPECT(a->caps & EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE,
            "active workspace does not advertise activate capability");
        printf("[client] active is (%u,%u) name=%s\n", a->coords[0], a->coords[1],
            a->name);
    }

    if (do_activate)
    {
        struct workspace *t = find_by_coords(activate_x, activate_y);
        EXPECT(t, "no workspace with coordinates (%u,%u)", activate_x, activate_y);
        printf("[client] activating (%u,%u) name=%s\n", activate_x, activate_y,
            t->name);
        ext_workspace_handle_v1_activate(t->handle);
        ext_workspace_manager_v1_commit(g.manager);

        /* The request may be handled by an animating plugin (e.g. vswitch):
         * the active state lands after the animation. Poll up to ~3s. */
        struct workspace *a = NULL;
        for (int i = 0; i < 60; i++)
        {
            wl_display_roundtrip(g.display);
            a = find_active();
            if (a && (a->coords[0] == then_x) && (a->coords[1] == then_y))
            {
                break;
            }

            usleep(50000);
        }

        EXPECT(a, "no active workspace after activate");
        EXPECT((a->coords[0] == then_x) && (a->coords[1] == then_y),
            "active workspace is (%u,%u) after activate, expected (%u,%u)",
            a->coords[0], a->coords[1], then_x, then_y);
        printf("[client] active switched to (%u,%u)\n", a->coords[0], a->coords[1]);
    }

    if (do_stop)
    {
        ext_workspace_manager_v1_stop(g.manager);
        wl_display_roundtrip(g.display);
        wl_display_roundtrip(g.display);
        EXPECT(g.finished, "no finished event after stop request");
    }

    printf("PASS ext-workspace-client\n");
    return 0;
}