/* SPDX-License-Identifier: GPL-2.0-only */
/* output-menu — per-output menu runtime: grab, open stack, hit-testing. */
#pragma once

#include "menu-state.hpp"

#include <wayfire/output.hpp>
#include <wayfire/plugins/common/input-grab.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-input.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/util.hpp>
#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/nonstd/json.hpp>

#include <wayfire/debug.hpp>

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

    /** @return Whether a menu tree is currently open on this output. */
    bool is_open() const
    {
        return !stack.empty();
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
    std::unique_ptr<wf::input_grab_t> input_grab;
    class MenuNode;
    class MenuRenderInstance;
    std::shared_ptr<MenuNode> node;

    std::vector<OpenLevel> stack;
    bool opened_by_press = false;
    uint64_t open_msec   = 0;

    void layout_level(OpenLevel &level, Menu *menu, wf::point_t pos);
    void open_submenu(OpenLevel &parent_level, MenuItem *item);
    void close_to_level(size_t depth);
    void sync_damage();
    void activate(MenuItem *item);
    void run_actions(MenuItem *item);
    MenuItem *hovered_item(wf::pointf_t global, size_t *depth = nullptr);

    static uint64_t now_msec();

    friend class MenuNode;
};

} // namespace obmenu
