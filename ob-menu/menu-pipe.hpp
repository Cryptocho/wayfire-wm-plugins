/* SPDX-License-Identifier: GPL-2.0-only */
/* menu-pipe — async pipemenus (M6). Mirrors labwc's pipe handling (vendor
 * reference: labwc@5aaa5da0571b src/menu/menu.c): lazy spawn at open time,
 * wl_event_loop fd + 4 s timer sources, 1 MiB output cap, '<' validation,
 * a waiting flag that blocks item selection, and content reset when the
 * tree closes so the next open re-spawns. New code, not vendored. */
#pragma once

#include "menu-model.hpp"

#include <wayfire/geometry.hpp>

#include <memory>
#include <string>
#include <sys/types.h>

struct wl_event_source;

namespace wf
{
class output_t;
}

namespace obmenu
{

class OutputMenu;
class PipeRunner;

static constexpr uint32_t PIPEMENU_TIMEOUT_MS = 4000; /* labwc: 4 seconds */
static constexpr size_t PIPEMENU_MAX_BUF_SIZE = 1048576; /* labwc: 1 MiB */

/* One in-flight pipemenu spawn (labwc menu_pipe_context). */
struct PipeSpawn
{
    PipeRunner *runner = nullptr;
    pid_t pid = -1;
    int pipe_fd = -1;
    std::string buf;
    Menu *pipemenu = nullptr;
    /* attach point of the parsed output: nullptr = opens as a root level,
     * otherwise the item in the parent level whose submenu this is */
    MenuItem *via_item = nullptr;
    wf::point_t anchor{};
    wl_event_source *fd_src    = nullptr;
    wl_event_source *timer_src = nullptr;
};

class PipeRunner
{
  public:
    explicit PipeRunner(OutputMenu *owner) : owner(owner)
    {}

    ~PipeRunner();
    PipeRunner(const PipeRunner&) = delete;
    PipeRunner& operator=(const PipeRunner&) = delete;

    /* Fork @pipemenu's execute command and watch its stdout. via_item ==
     * nullptr opens the parsed menu as a root level, otherwise as a
     * submenu of the level containing @via_item. */
    bool spawn_async(Menu *pipemenu, MenuItem *via_item, wf::point_t anchor);

    /* Abandon a pending spawn: remove the sources, close the read end.
     * The child is left to exit on its own (SIGPIPE on its next write),
     * matching labwc's pipemenu_ctx_destroy. */
    void cancel();

    bool waiting = false;

  private:
    static int on_readable(int fd, uint32_t mask, void *data);
    static int on_timeout(void *data);
    /* deliver the parsed output to the owner, then cancel */
    void complete(bool open_result);

    OutputMenu *owner;
    std::unique_ptr<PipeSpawn> ctx;
};

/* labwc reset_pipemenus: drop pipemenu-generated content when the whole
 * tree closes — inline submenus are freed, pipemenu definitions are
 * emptied so they re-spawn on the next open. */
void reset_pipemenus(MenuSet &set);
} // namespace obmenu
