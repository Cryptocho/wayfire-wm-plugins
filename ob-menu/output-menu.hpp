/* SPDX-License-Identifier: GPL-2.0-only */
/* output-menu — per-output menu runtime: grab, open stack, hit-testing. */
#pragma once

#include "menu-state.hpp"
#include "menu-pipe.hpp"

#include <wayfire/output.hpp>
#include <wayfire/plugins/common/input-grab.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-input.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/util.hpp>
#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/nonstd/json.hpp>

#include <wayfire/debug.hpp>
#include <wayfire/toplevel-view.hpp>

#include <linux/input-event-codes.h>

#include <memory>
#include <vector>

namespace obmenu
{

class OutputMenu : public wf::pointer_interaction_t,
    public wf::keyboard_interaction_t, public wf::touch_interaction_t
{
  public:
    explicit OutputMenu(wf::output_t *output);
    ~OutputMenu();

    /** Open a menu tree at the given output-local anchor point. */
    void open_root(const std::string &id, wf::pointf_t anchor);

    /** Open the client-menu anchored to the titlebar of @view. */
    void open_client_menu(wayfire_toplevel_view view);

    /** Open the client-menu at an explicit output-local anchor. */
    void open_client_menu(wayfire_toplevel_view view, wf::pointf_t anchor);

    /** Remember the last focused toplevel (for client-menu actions). */
    void set_focused_view(wayfire_toplevel_view view)
    {
        focused_view = view;
    }

    /** @return Whether the menu system is active on this output (a tree is
     * open, or a pipemenu spawn is pending that opened it). */
    bool is_open() const
    {
        return tree_active;
    }

    /** Selection state for IPC assertions. */
    wf::json_t get_state();

    /* --- input entry points (grab routes all events here) --- */
    void handle_pointer_enter(wf::pointf_t global) override
    {
        handle_pointer_motion(global);
    }

    void handle_pointer_leave() override
    {}

    void handle_pointer_motion(wf::pointf_t global, uint32_t time = 0);
    void handle_pointer_button(const wlr_pointer_button_event& ev) override
    {
        bool dismissed = false;
        handle_pointer_button(ev, &dismissed);
    }

    void handle_keyboard_key(wf::seat_t *seat,
        wlr_keyboard_key_event event) override;

    /* Navigation shared by real keys and IPC-simulated keys. */
    void handle_nav_key(const std::string &key);
    void handle_touch_down(uint32_t, int, wf::pointf_t position) override
    {
        handle_pointer_motion(position);
    }

    void handle_touch_up(uint32_t, int, wf::pointf_t) override
    {}

    void handle_touch_motion(uint32_t, int, wf::pointf_t position) override
    {
        handle_pointer_motion(position);
    }

    /* Render the open stack (called by the scene render instance). */
    void render(const wf::scene::render_instruction_t& data);

    /* PipeRunner callback: open the parsed pipemenu (as root level when
     * ctx.via_item == nullptr, else as the submenu of its parent item). */
    void open_pipe_level(PipeSpawn &ctx);

    /* --- input entry points (called by the plugin's interactions) --- */
    void handle_pointer_motion_impl(wf::pointf_t global);
    void handle_pointer_button(const wlr_pointer_button_event& ev,
        bool *dismissed);
    void close_all(bool restore = true);

  private:
    struct OpenLevel
    {
        Menu *menu = nullptr;
        wf::geometry_t box{};
        std::vector<wf::geometry_t> item_rects;
        int selected = -1;
        bool align_left = true;
        /* the item in the PARENT menu through which this level was opened */
        MenuItem *via_item = nullptr;
    };

    wf::output_t *output;
    wayfire_toplevel_view focused_view;
    std::unique_ptr<wf::input_grab_t> input_grab;
    class MenuNode;
    class MenuRenderInstance;
    std::shared_ptr<MenuNode> node;

    std::vector<OpenLevel> stack;
    bool opened_by_press = false;
    uint64_t open_msec   = 0;
    /* menu system active (grab held, node mounted) even with an empty
     * stack — a root pipemenu spawn is pending or failed */
    bool tree_active = false;
    PipeRunner pipe;

    void layout_level(OpenLevel &level, Menu *menu, wf::point_t pos);
    void open_submenu(size_t level_idx, MenuItem *item);
    void close_to_level(size_t depth);
    void sync_damage();
    void activate(MenuItem *item);
    void run_actions(MenuItem *item);
    MenuItem *hovered_item(wf::pointf_t global, size_t *depth = nullptr);

    static uint64_t now_msec();

    friend class MenuNode;
    friend class PipeRunner;
};

} // namespace obmenu
