/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vinput — inject real input events into a (headless/nested) wayfire session
 * via the zwp_virtual_keyboard / zwlr_virtual_pointer protocols.
 *
 * Purpose: ob-menu's IPC tests bypass the grab/interaction input path;
 * this tool exercises it for real (grab node -> interaction routing),
 * catching regressions like a NULL interaction pointer.
 *
 * Usage:
 *   vinput <WAYLAND_DISPLAY> move <dx> <dy>       relative pointer motion
 *   vinput <WAYLAND_DISPLAY> click <btn>          press+release (0x110=LEFT)
 *   vinput <WAYLAND_DISPLAY> button <btn> <0|1>
 *   vinput <WAYLAND_DISPLAY> key <evdev> <0|1>
 * Commands are applied in order; WAYLAND_DISPLAY is also passed as an
 * argument so the wrapper scripts can target the test session.
 */
#define _GNU_SOURCE
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

static uint32_t g_time = 0;
static struct zwp_virtual_keyboard_manager_v1 *kbd_mgr;
static struct zwlr_virtual_pointer_manager_v1 *ptr_mgr;
static struct wl_seat *seat;

static void handle_global(void *data, struct wl_registry *reg, uint32_t name,
    const char *iface, uint32_t version)
{
    (void)data;
    if (strcmp(iface, "wl_seat") == 0)
    {
        seat = wl_registry_bind(reg, name, &wl_seat_interface,
            version < 5 ? version : 5);
    }
    else if (strcmp(iface, "zwp_virtual_keyboard_manager_v1") == 0)
    {
        kbd_mgr = wl_registry_bind(reg, name,
            &zwp_virtual_keyboard_manager_v1_interface, 1);
    }
    else if (strcmp(iface, "zwlr_virtual_pointer_manager_v1") == 0)
    {
        ptr_mgr = wl_registry_bind(reg, name,
            &zwlr_virtual_pointer_manager_v1_interface, 1);
    }
}

static void handle_global_remove(void *data, struct wl_registry *reg,
    uint32_t name)
{
    (void)data;
    (void)reg;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr,
            "usage: vinput <WAYLAND_DISPLAY> CMD...\n"
            "  CMD: move <dx> <dy> | click <btn> | button <btn> <0|1>\n"
            "       key <evdev-code> <0|1>\n");
        return 2;
    }

    setenv("WAYLAND_DISPLAY", argv[1], 1);
    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy)
    {
        fprintf(stderr, "vinput: wl_display_connect failed\n");
        return 1;
    }

    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(dpy);
    wl_display_roundtrip(dpy);
    if (!seat || !kbd_mgr || !ptr_mgr)
    {
        fprintf(stderr, "vinput: missing globals (seat=%p vkbd=%p vptr=%p)\n",
            seat, kbd_mgr, ptr_mgr);
        return 1;
    }

    struct zwp_virtual_keyboard_v1 *kbd =
        zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(kbd_mgr, seat);
    struct zwlr_virtual_pointer_v1 *ptr =
        zwlr_virtual_pointer_manager_v1_create_virtual_pointer(ptr_mgr, seat);
    if (!kbd || !ptr)
    {
        fprintf(stderr, "vinput: device creation failed\n");
        return 1;
    }

    /* minimal us keymap so xkb modifiers/keycodes resolve */
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_rule_names names = {
        .rules = "evdev", .model = "pc105", .layout = "us",
    };
    struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, &names,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!km)
    {
        fprintf(stderr, "vinput: keymap compile failed\n");
        return 1;
    }

    /* Local xkb state mirror: wlroots does NOT update the server-side
     * xkb state from key events (update_state=false in
     * virtual_keyboard_key); the protocol requires the client to report
     * modifier state via the modifiers request. We track it locally and
     * serialize after every key. */
    struct xkb_state *xs = xkb_state_new(km);
    if (!xs)
    {
        fprintf(stderr, "vinput: xkb_state failed\n");
        return 1;
    }

    const char *text = xkb_keymap_get_as_string(km,
        XKB_KEYMAP_FORMAT_TEXT_V1);
    size_t len = strlen(text) + 1;
    FILE *f = tmpfile(); /* anonymous temp file, fd passed to the server */
    if (!f || (fwrite(text, 1, len, f) != len) || (fflush(f) != 0))
    {
        fprintf(stderr, "vinput: keymap fd setup failed\n");
        return 1;
    }

    long klen = ftell(f);
    zwp_virtual_keyboard_v1_keymap(kbd, 1 /* XKB_V1 */, fileno(f),
        (uint32_t)klen);
    xkb_context_unref(ctx);

    for (int i = 2; i < argc;)
    {
        if (!strcmp(argv[i], "move") && (i + 2 < argc))
        {
            zwlr_virtual_pointer_v1_motion(ptr, g_time,
                wl_fixed_from_double(atof(argv[i + 1])),
                wl_fixed_from_double(atof(argv[i + 2])));
            zwlr_virtual_pointer_v1_frame(ptr);
            i += 3;
        }
        else if (!strcmp(argv[i], "click") && (i + 1 < argc))
        {
            uint32_t btn = (uint32_t)strtoul(argv[i + 1], NULL, 0);
            zwlr_virtual_pointer_v1_button(ptr, g_time, btn, 1);
            zwlr_virtual_pointer_v1_frame(ptr);
            zwlr_virtual_pointer_v1_button(ptr, g_time + 10, btn, 0);
            zwlr_virtual_pointer_v1_frame(ptr);
            i += 2;
            g_time += 10;
        }
        else if (!strcmp(argv[i], "button") && (i + 2 < argc))
        {
            zwlr_virtual_pointer_v1_button(ptr, g_time,
                (uint32_t)strtoul(argv[i + 1], NULL, 0),
                (uint32_t)atoi(argv[i + 2]));
            zwlr_virtual_pointer_v1_frame(ptr);
            i += 3;
        }
        else if (!strcmp(argv[i], "key") && (i + 2 < argc))
        {
            uint32_t evdev = (uint32_t)strtoul(argv[i + 1], NULL, 0);
            uint32_t st    = (uint32_t)atoi(argv[i + 2]);
            /* wlr_keyboard_key_event.keycode is the evdev code; wlroots
             * applies the xkb +8 internally, so pass the raw code */
            zwp_virtual_keyboard_v1_key(kbd, g_time, evdev, st);
            /* mirror the key in our local xkb state and report the new
             * modifier mask (the server never derives it from keys) */
            xkb_state_update_key(xs, evdev + 8,
                st ? XKB_KEY_DOWN : XKB_KEY_UP);
            zwp_virtual_keyboard_v1_modifiers(kbd,
                xkb_state_serialize_mods(xs, XKB_STATE_MODS_DEPRESSED),
                xkb_state_serialize_mods(xs, XKB_STATE_MODS_LATCHED),
                xkb_state_serialize_mods(xs, XKB_STATE_MODS_LOCKED),
                xkb_state_serialize_layout(xs, XKB_STATE_LAYOUT_EFFECTIVE));
            i += 3;
        }
        else
        {
            fprintf(stderr, "vinput: bad cmd '%s'\n", argv[i]);
            return 2;
        }

        g_time += 10;
    }

    wl_display_flush(dpy);
    if (wl_display_roundtrip(dpy) < 0)
    {
        fprintf(stderr, "vinput: protocol error / connection closed\n");
        return 1;
    }

    zwlr_virtual_pointer_v1_destroy(ptr);
    zwp_virtual_keyboard_v1_destroy(kbd);
    xkb_state_unref(xs);
    xkb_keymap_unref(km);
    wl_display_flush(dpy);
    wl_display_disconnect(dpy);
    return 0;
}
