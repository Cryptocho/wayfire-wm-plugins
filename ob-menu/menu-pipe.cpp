/* SPDX-License-Identifier: GPL-2.0-only */
/* menu-pipe — implementation (see menu-pipe.hpp for the design notes). */
#include "menu-pipe.hpp"
#include "menu-parse.hpp"
#include "menu-state.hpp"
#include "output-menu.hpp"

#include <wayfire/core.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/nonstd/wlroots.hpp>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace obmenu
{
PipeRunner::~PipeRunner()
{
    cancel();
}

bool PipeRunner::spawn_async(Menu *pipemenu, MenuItem *via_item,
    wf::point_t anchor)
{
    /* labwc asserts !pipe_ctx; only one spawn can be pending */
    if (ctx)
    {
        return false;
    }

    /* Double-fork per wayfire core.cpp run(): the intermediate process
     * reports the real child's pid and exits, so the child is orphaned and
     * reaped by init — we never need a SIGCHLD handler. */
    int pid_fds[2]; /* CLOEXEC: carries the child pid back */
    int out_fds[2]; /* child stdout → our read end */
    if (pipe2(pid_fds, O_CLOEXEC) != 0)
    {
        LOGE("ob-menu: pipe2() failed: ", strerror(errno));
        return false;
    }

    if (pipe(out_fds) != 0)
    {
        LOGE("ob-menu: pipe() failed: ", strerror(errno));
        close(pid_fds[0]);
        close(pid_fds[1]);
        return false;
    }

    pid_t mid = fork();
    if (mid < 0)
    {
        LOGE("ob-menu: fork() failed: ", strerror(errno));
        close(pid_fds[0]);
        close(pid_fds[1]);
        close(out_fds[0]);
        close(out_fds[1]);
        return false;
    }

    if (mid == 0)
    {
        /* intermediate process */
        pid_t child = fork();
        if (child < 0)
        {
            _exit(1);
        }

        if (child == 0)
        {
            /* real child (labwc spawn_piped): stdout → pipe,
             * stdin/stderr → /dev/null */
            close(out_fds[0]);
            dup2(out_fds[1], STDOUT_FILENO);
            close(out_fds[1]);
            int dn = open("/dev/null", O_RDWR);
            if (dn >= 0)
            {
                dup2(dn, STDIN_FILENO);
                dup2(dn, STDERR_FILENO);
                close(dn);
            }

            execl("/bin/sh", "sh", "-c", pipemenu->execute.c_str(),
                (char*)nullptr);
            _exit(1);
        }

        close(out_fds[0]);
        close(out_fds[1]);
        ssize_t r = write(pid_fds[1], &child, sizeof(child));
        _exit(r == sizeof(child) ? 0 : 1);
    }

    close(pid_fds[1]);
    close(out_fds[1]);

    int status  = 0;
    pid_t child = -1;
    waitpid(mid, &status, 0);
    bool ok = WIFEXITED(status) && (WEXITSTATUS(status) == 0) &&
        (read(pid_fds[0], &child, sizeof(child)) == sizeof(child)) &&
        (child > 0);
    close(pid_fds[0]);
    if (!ok)
    {
        LOGE("ob-menu: failed to spawn pipemenu '", pipemenu->execute, "'");
        close(out_fds[0]);
        return false;
    }

    /* don't leak the read end to later spawns */
    fcntl(out_fds[0], F_SETFD, FD_CLOEXEC);

    ctx = std::make_unique<PipeSpawn>();
    ctx->runner   = this;
    ctx->pid      = child;
    ctx->pipe_fd  = out_fds[0];
    ctx->pipemenu = pipemenu;
    ctx->via_item = via_item;
    ctx->anchor   = anchor;

    auto loop    = wf::get_core().ev_loop;
    ctx->fd_src  = wl_event_loop_add_fd(loop, out_fds[0], WL_EVENT_READABLE,
        on_readable, ctx.get());
    ctx->timer_src = wl_event_loop_add_timer(loop, on_timeout, ctx.get());
    wl_event_source_timer_update(ctx->timer_src, PIPEMENU_TIMEOUT_MS);
    waiting = true;
    LOGI("ob-menu: [pipemenu ", (long)child, "] executed: ", pipemenu->execute);
    return true;
}

void PipeRunner::cancel()
{
    if (ctx)
    {
        if (ctx->fd_src)
        {
            wl_event_source_remove(ctx->fd_src);
        }

        if (ctx->timer_src)
        {
            wl_event_source_remove(ctx->timer_src);
        }

        if (ctx->pipe_fd >= 0)
        {
            close(ctx->pipe_fd);
        }

        ctx.reset();
    }

    waiting = false;
}

void PipeRunner::complete(bool open_result)
{
    if (open_result && owner && ctx)
    {
        owner->open_pipe_level(*ctx);
    }

    cancel();
}

int PipeRunner::on_readable(int fd, uint32_t mask, void *data)
{
    (void)fd;
    (void)mask;
    auto *sp = static_cast<PipeSpawn *>(data);
    char chunk[8193]; /* two 4k pages + 1 NULL byte (labwc) */
    ssize_t size;
    do
    {
        size = read(sp->pipe_fd, chunk, sizeof(chunk) - 1);
    } while ((size == -1) && (errno == EINTR));

    if (size == -1)
    {
        LOGE("ob-menu: [pipemenu ", (long)sp->pid, "] read failed: ",
            strerror(errno));
        sp->runner->complete(false);
        return 0;
    }

    if ((size > 0) &&
        (sp->buf.size() + (size_t)size > PIPEMENU_MAX_BUF_SIZE))
    {
        LOGE("ob-menu: [pipemenu ", (long)sp->pid, "] too big (> ",
            PIPEMENU_MAX_BUF_SIZE, " bytes); killing ", sp->pipemenu->execute);
        kill(sp->pid, SIGTERM);
        sp->runner->complete(false);
        return 0;
    }

    if (size > 0)
    {
        sp->buf.append(chunk, size);
        /* wait for EOF before parsing (labwc does the same) */
        return 0;
    }

    /* EOF: guard against badly formed data such as binary input */
    size_t start = sp->buf.find_first_not_of(" \t\r\n");
    if ((start == std::string::npos) || (sp->buf[start] != '<'))
    {
        LOGE("ob-menu: pipemenu output does not start with '<'; aborting");
        sp->runner->complete(false);
        return 0;
    }

    auto &state = MenuState::get();
    Menu *menu  = sp->pipemenu;
    menu->items.clear(); /* should already be empty; defensive */
    parse_pipe_menu_buffer(sp->buf.data() + start, sp->buf.size() - start,
        menu, state.set);
    /* parse failure leaves no items: keep the tree open but empty */
    sp->runner->complete(!menu->items.empty());
    return 0;
}

int PipeRunner::on_timeout(void *data)
{
    auto *sp = static_cast<PipeSpawn *>(data);
    LOGE("ob-menu: [pipemenu ", (long)sp->pid,
        "] timeout reached, killing ", sp->pipemenu->execute);
    kill(sp->pid, SIGTERM);
    sp->runner->complete(false);
    return 0;
}

void reset_pipemenus(MenuSet &set)
{
    for (auto it = set.menus.begin(); it != set.menus.end();)
    {
        Menu *menu = it->second.get();
        if (menu->is_pipemenu_child)
        {
            /* inline submenus created by pipemenu output (labwc menu_free) */
            it = set.menus.erase(it);
        } else if (!menu->execute.empty())
        {
            /* pipemenu definitions: empty so the next open re-spawns */
            menu->items.clear();
            menu->is_pipemenu_cached = false;
            ++it;
        } else
        {
            ++it;
        }
    }
}
} // namespace obmenu
