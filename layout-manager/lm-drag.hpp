/* SPDX-License-Identifier: GPL-2.0-only */
/* Drag-to-reorder + drop preview for layout-manager tiling (sway-style:
 * drag a tiled view with the move button, drop it over another tiled view
 * to insert it left/right/above/below, or drop it in the center to swap).
 *
 * Derived from Wayfire plugins/tile/tile-dragging.hpp (GPL-2.0-only,
 * (c) Wayfire developers); adapted to the lm fraction-based tree:
 *   - swap keeps the two views' geometries (fractions) intact
 *   - insert uses the fraction model (the moved view takes a fresh share)
 *   - the window motion is driven by a view 2D transformer owned by the
 *     plugin (the shared move_drag::core_drag_t is a static library in
 *     wayfire's build tree and cannot be linked from an out-of-tree
 *     plugin), so this header only tracks the drop destination/preview. */
#pragma once

#include "lm-tree.hpp"
#include "lm-wset.hpp"
#include "lm-float.hpp"
#include "lm-animator.hpp"

#include <wayfire/plugins/common/preview-indication.hpp>

namespace lm
{
enum split_insertion_t
{
    INSERT_NONE  = 0,
    INSERT_ABOVE = 1,
    INSERT_BELOW = 2,
    INSERT_LEFT  = 3,
    INSERT_RIGHT = 4,
    INSERT_SWAP  = 5,
};

/**
 * Find the position of the split to be created if a view is dropped at
 * @input over @node. By default 1/3rd of the view's edges are "active".
 */
inline split_insertion_t calculate_insert_type(
    nonstd::observer_ptr<tree_node_t> node, wf::point_t input, double sensitivity)
{
    auto window = node->geometry;
    if (!(window & input))
    {
        return INSERT_NONE;
    }

    std::vector<std::pair<double, split_insertion_t>> edges;
    double px = 1.0 * (input.x - window.x) / window.width;
    double py = 1.0 * (input.y - window.y) / window.height;

    edges.push_back({px, INSERT_LEFT});
    edges.push_back({py, INSERT_ABOVE});
    edges.push_back({1.0 - px, INSERT_RIGHT});
    edges.push_back({1.0 - py, INSERT_BELOW});

    auto it = std::remove_if(edges.begin(), edges.end(),
        [sensitivity] (auto pair) { return pair.first > sensitivity; });
    edges.erase(it, edges.end());

    if (edges.empty())
    {
        return INSERT_SWAP;
    }

    return std::min_element(edges.begin(), edges.end())->second;
}

/** Bound of the drop preview rectangle (a fraction of the target node). */
inline wf::geometry_t calculate_split_preview(
    nonstd::observer_ptr<tree_node_t> over, split_insertion_t split_type)
{
    static constexpr double SPLIT_PREVIEW_PERCENTAGE = 1.0 / 3.0;

    auto preview = over->geometry;
    switch (split_type)
    {
      case INSERT_RIGHT:
        preview.x += preview.width * (1 - SPLIT_PREVIEW_PERCENTAGE);
        [[fallthrough]];

      case INSERT_LEFT:
        preview.width = preview.width * SPLIT_PREVIEW_PERCENTAGE;
        break;

      case INSERT_BELOW:
        preview.y += preview.height * (1 - SPLIT_PREVIEW_PERCENTAGE);
        [[fallthrough]];

      case INSERT_ABOVE:
        preview.height = preview.height * SPLIT_PREVIEW_PERCENTAGE;
        break;

      default:
        break;
    }

    return preview;
}

/**
 * Drop-destination tracking + executing the drop while a tiled view is
 * being drag-reordered by the plugin (which drives the window motion with
 * its own transformer). Renders a preview of the drop position.
 */
class drag_manager_t
{
  public:
    bool drop_suppressed = false;

    /** The next drop must not run (grab stolen / plugin teardown). */
    void suppress_next_drop()
    {
        drop_suppressed = true;
    }

    /** Hide the drop preview (end of drag, or drag aborted). */
    void hide_preview()
    {
        if (this->preview)
        {
            auto target = preview->get_output() ?
                preview->get_output()->get_cursor_position().round_down() :
                wf::point_t{0, 0};
            this->preview->set_target_geometry(target, 0.0, true);
            this->preview.reset();
        }
    }

    /**
     * Update the preview for the given dragged view at the current cursor
     * position (wset-global @input).
     */
    void update_preview(wayfire_toplevel_view dragged_view, wf::point_t input)
    {
        auto output = dragged_view->get_output();
        auto target = check_drop_destination(output, input, dragged_view);
        if (!target)
        {
            return hide_preview();
        }

        auto split = calculate_insert_type(target, input, SPLIT_PREVIEW_PERCENTAGE);
        if (preview && (preview->get_output() != output))
        {
            hide_preview();
        }

        if (!this->preview)
        {
            auto start_coords = lm::get_wset_local_coordinates(
                output->wset(), input);
            preview = std::make_shared<wf::preview_indication_t>(
                start_coords, output, "layout-manager");
        }

        auto preview_geometry = calculate_split_preview(target, split);
        preview_geometry = lm::get_wset_local_coordinates(output->wset(),
            preview_geometry);
        if (preview_geometry != preview->get_target_geometry())
        {
            this->preview->set_target_geometry(preview_geometry, 1.0);
        }
    }

    /** The node under @global_coords which is a suitable drop target. */
    nonstd::observer_ptr<lm::view_node_t> check_drop_destination(
        wf::output_t *output, wf::point_t global_coords,
        wayfire_toplevel_view dragged_view)
    {
        if (!output->wset() ||
            !output->wset()->has_data<lm_workspace_set_data_t>())
        {
            return nullptr;
        }

        auto &data = lm_workspace_set_data_t::get(output);
        auto ws    = output->wset()->get_current_workspace();
        auto dropped_at = lm::find_view_at(data.roots[ws.x][ws.y], global_coords);
        if (!dropped_at || (dropped_at->view == dragged_view))
        {
            return nullptr;
        }

        return dropped_at;
    }

    /** Attempt to drop @view at @input (wset-global) on @output.
     * Returns true if a reorder happened. */
    bool handle_drop(wayfire_toplevel_view view, wf::output_t *output,
        wf::point_t input)
    {
        if (drop_suppressed)
        {
            drop_suppressed = false;
            hide_preview();
            return false;
        }

        auto dropped_at = check_drop_destination(output, input, view);
        if (!dropped_at)
        {
            hide_preview();
            return false;
        }

        auto split = calculate_insert_type(dropped_at, input,
            SPLIT_PREVIEW_PERCENTAGE);
        hide_preview();

        if (split == INSERT_NONE)
        {
            return false;
        }

        if (split == INSERT_SWAP)
        {
            handle_swap(view, dropped_at->view);
        } else
        {
            handle_move_retile(view, dropped_at, split);
        }

        return true;
    }

  private:
    static constexpr double SPLIT_PREVIEW_PERCENTAGE = 1.0 / 3.0;

    std::shared_ptr<wf::preview_indication_t> preview;

    /** Swap the tree nodes of two views, preserving their geometries
     * (fractions). Same-wset only. */
    void handle_swap(wayfire_toplevel_view a, wayfire_toplevel_view b)
    {
        lm::autocommit_transaction_t tx;
        auto node_a = lm::view_node_t::get_node(a);
        auto node_b = lm::view_node_t::get_node(b);
        if (!node_a || !node_b)
        {
            return;
        }

        auto parent_a = node_a->parent;
        auto parent_b = node_b->parent;
        auto gaps_a   = node_a->get_gaps();
        auto gaps_b   = node_b->get_gaps();
        double frac_a = node_a->fraction;
        double frac_b = node_b->fraction;
        int idx_a = lm::remove_child_no_arange(node_a);
        int idx_b = lm::remove_child_no_arange(node_b);

        /* Nodes keep their geometry/fraction: build fresh nodes with the
         * swapped slots. */
        auto new_b = std::make_unique<lm::view_node_t>(b);
        new_b->set_gaps(gaps_a);
        new_b->fraction = frac_a;

        auto new_a = std::make_unique<lm::view_node_t>(a);
        new_a->set_gaps(gaps_b);
        new_a->fraction = frac_b;

        new_a->parent = parent_b;
        new_b->parent = parent_a;

        if ((parent_a != parent_b) || (idx_a < idx_b))
        {
            parent_a->children.insert(parent_a->children.begin() + idx_a,
                std::move(new_b));
            parent_b->children.insert(parent_b->children.begin() + idx_b,
                std::move(new_a));
        } else
        {
            parent_b->children.insert(parent_b->children.begin() + idx_b,
                std::move(new_a));
            parent_a->children.insert(parent_a->children.begin() + idx_a,
                std::move(new_b));
        }

        lm_workspace_set_data_t::get(a->get_wset()).refresh(tx.tx);
    }

    /** Insert the dragged view as sibling of @target in the given
     * direction, inside the target's parent split. */
    void handle_move_retile(wayfire_toplevel_view source,
        nonstd::observer_ptr<lm::view_node_t> target, split_insertion_t split)
    {
        auto source_node = lm::view_node_t::get_node(source);
        if (!source_node || source->get_wset() != target->view->get_wset())
        {
            return; /* cross-wset drops are not reordered */
        }

        lm::autocommit_transaction_t tx;
        auto split_type = (split == INSERT_LEFT || split == INSERT_RIGHT) ?
            SPLIT_VERTICAL : SPLIT_HORIZONTAL;

        if (target->parent->get_split_direction() == split_type)
        {
            /* Add the dragged view as a sibling of the target view */
            auto src = source_node->parent->remove_child(source_node, tx.tx);
            int idx  = 0;
            for (size_t i = 0; i < target->parent->children.size(); i++)
            {
                if (target->parent->children[i].get() == target.get())
                {
                    idx = (int)i;
                    break;
                }
            }

            if ((split == INSERT_RIGHT) || (split == INSERT_BELOW))
            {
                ++idx;
            }

            target->parent->add_child(std::move(src), tx.tx, idx);
        } else
        {
            /* Wrap the dragged and target views in a new split */
            auto new_split = std::make_unique<lm::split_node_t>(split_type);
            new_split->set_geometry(target->geometry, tx.tx);

            int idx = 0;
            for (size_t i = 0; i < target->parent->children.size(); i++)
            {
                if (target->parent->children[i].get() == target.get())
                {
                    idx = (int)i;
                    break;
                }
            }

            auto dropped_parent = target->parent;
            auto dropped_view   = target->parent->remove_child(target, tx.tx);
            auto dragged_view   = source_node->parent->remove_child(
                source_node, tx.tx);

            if ((split == INSERT_ABOVE) || (split == INSERT_LEFT))
            {
                new_split->add_child(std::move(dragged_view), tx.tx);
                new_split->add_child(std::move(dropped_view), tx.tx);
            } else
            {
                new_split->add_child(std::move(dropped_view), tx.tx);
                new_split->add_child(std::move(dragged_view), tx.tx);
            }

            dropped_parent->add_child(std::move(new_split), tx.tx, idx);
        }

        lm_workspace_set_data_t::get(source->get_wset()).refresh(tx.tx);
    }
};
}