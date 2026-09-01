/* SPDX-License-Identifier: GPL-2.0-only */
/* Geometry animations for layout-manager tiling: pure display-layer
 * interpolation. The real geometry is committed through transactions on
 * the first frame (as before); a 2D view transformer only interpolates
 * what the user sees from the old to the new box, so automated geometry
 * assertions stay exact while the motion is smooth.
 *
 * The transformer math is derived from Wayfire plugins/tile
 * (scale_transformer_t, GPL-2.0-only, (c) Wayfire developers).
 * The ob-decor frame lives inside the transformed subtree (the transformer
 * replaces the surface root node), so the whole decorated window glides
 * to its new box and snaps back to 1:1 when the animation ends. */
#pragma once

#include <map>
#include <set>

#include <wayfire/plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/plugins/common/util.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/plugins/common/geometry-animation.hpp>

namespace lm
{
static const std::string lm_scale_anim_name = "layout-manager-scale-anim";

/**
 * Scale/translate the view so that its displayed (wm) geometry region is
 * the given box — same math as Wayfire tile's scale_transformer_t.
 */
class lm_scale_transformer_t : public wf::scene::view_2d_transformer_t
{
  public:
    lm_scale_transformer_t(wayfire_toplevel_view view) :
        wf::scene::view_2d_transformer_t(view)
    {}

    void set_target_box(wf::geometry_t box)
    {
        auto _view = view.lock();
        if (!_view)
        {
            return;
        }

        auto current = toplevel_cast(_view.get())->get_geometry();
        if ((current.width <= 0) || (current.height <= 0))
        {
            return;
        }

        _view->damage();

        double scale_horiz = 1.0 * box.width / current.width;
        double scale_vert  = 1.0 * box.height / current.height;

        /* Position of the top-left corner after scaling (center anchor) */
        double scaled_x = current.x + (current.width / 2.0 * (1 - scale_horiz));
        double scaled_y = current.y + (current.height / 2.0 * (1 - scale_vert));

        this->scale_x       = scale_horiz;
        this->scale_y       = scale_vert;
        this->translation_x = box.x - scaled_x;
        this->translation_y = box.y - scaled_y;

        _view->damage();
    }
};

/**
 * Plugin-level animation manager (singleton): map of view -> running
 * geometry animation. Covers tree-internal changes (attach/detach/harvest
 * — driven from view_node_t::set_geometry) and tree-external ones (float
 * toggle, stacking restore), which tile's per-node implementation cannot
 * reach.
 */
class layout_animator_t
{
  public:
    static layout_animator_t& get()
    {
        static layout_animator_t instance;
        return instance;
    }

    /**
     * Animate the view from `from` (its committed geometry at trigger
     * time) to the just-committed pending geometry. Skips silently when
     * disabled (duration 0), during divider drags (plugin capability
     * active) or when there is nothing to move.
     */
    void animate(wayfire_toplevel_view view, wf::geometry_t from)
    {
        if (!view || !view->get_output() || !enabled())
        {
            return;
        }

        auto wo = view->get_output();
        if (wo->is_plugin_active("layout-manager"))
        {
            /* divider/edge drag: geometry must follow the pointer 1:1 */
            return;
        }

        auto to = view->toplevel()->pending().geometry;
        if ((from == to) || (from.width <= 0) || (from.height <= 0))
        {
            return;
        }

        auto &entry = entries[view];
        entry.from  = from;
        entry.to    = to;
        entry.anim  = wf::animation::duration_t(duration);
        entry.anim.start();

        /* Named transformer: re-created (or kept) per view; start by
         * displaying the old box so the first committed frame does not
         * jump */
        entry.tr = wf::ensure_named_transformer<lm_scale_transformer_t>(
            view, wf::TRANSFORMER_2D, lm_scale_anim_name, view);
        entry.tr->set_target_box(from);

        ensure_hook(wo);
    }

    /** Drop the animation of a view (unmap, cancel-all paths). */
    void cancel(wayfire_toplevel_view view)
    {
        auto it = entries.find(view);
        if (it != entries.end())
        {
            finish(it);
        }
    }

    void cancel_all()
    {
        while (!entries.empty())
        {
            finish(entries.begin());
        }
    }

  private:
    struct view_anim_t
    {
        wf::geometry_t from{};
        wf::geometry_t to{};
        wf::animation::duration_t anim;
        nonstd::observer_ptr<lm_scale_transformer_t> tr;
    };

    wf::option_wrapper_t<wf::animation_description_t> duration{
        "layout-manager/animation_duration"};

    bool enabled() const
    {
        return duration.value().length_ms > 0;
    }

    std::map<wayfire_toplevel_view, view_anim_t> entries;

    /* One frame hook per output, registered while that output has active
     * animations (animate-plugin pattern: damage -> step -> damage). */
    wf::effect_hook_t frame_hook = [=] ()
    {
        update_frame();
    };

    std::set<wf::output_t*> hooked;

    void ensure_hook(wf::output_t *wo)
    {
        if (hooked.insert(wo).second)
        {
            wo->render->add_effect(&frame_hook, wf::OUTPUT_EFFECT_PRE);
        }
    }

    void unhook_if_idle(wf::output_t *wo)
    {
        for (auto& [view, entry] : entries)
        {
            if (view->get_output() == wo)
            {
                return; /* still busy */
            }
        }

        if (hooked.erase(wo))
        {
            wo->render->rem_effect(&frame_hook);
        }
    }

    using iterator = std::map<wayfire_toplevel_view, view_anim_t>::iterator;

    void finish(iterator it)
    {
        auto view = it->first;
        if (view && view->get_transformed_node())
        {
            view->get_transformed_node()->rem_transformer(lm_scale_anim_name);
            view->damage();
        }

        auto wo = view ? view->get_output() : nullptr;
        entries.erase(it);
        if (wo)
        {
            unhook_if_idle(wo);
        }
    }

    void update_frame()
    {
        std::vector<wayfire_toplevel_view> finished;
        for (auto& [view, entry] : entries)
        {
            if (!view || !view->is_mapped() || !view->get_output())
            {
                finished.push_back(view);
                continue;
            }

            view->damage();
            if (!entry.anim.running())
            {
                finished.push_back(view);
                continue;
            }

            auto displayed = wf::interpolate(entry.from,
                entry.to, entry.anim.progress());
            entry.tr->set_target_box(displayed);
            view->damage();
        }

        for (auto view : finished)
        {
            cancel(view);
        }
    }
};
}
