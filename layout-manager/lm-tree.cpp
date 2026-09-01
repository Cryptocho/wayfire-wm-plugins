/* SPDX-License-Identifier: GPL-2.0-only */
/* Derived from Wayfire plugins/tile/tree.cpp (GPL-2.0-only),
 * (c) Wayfire developers; modified for layout-manager:
 *   - children are arranged by explicit fractions following sway
 *     semantics (sway/tree/arrange.c:15-97): fresh children take the
 *     average of the existing shares, then everything is normalized
 *   - animations/crossfade removed: geometries are committed directly
 *     through transactions (avoids interference with SSD frame margins) */
#include "lm-tree.hpp"
#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/toplevel-view.hpp"
#include <wayfire/util/log.hpp>

#include <wayfire/output.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/window-manager.hpp>

#include <cmath>

namespace lm
{
void tree_node_t::set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr&)
{
    this->geometry = geometry;
}

nonstd::observer_ptr<split_node_t> tree_node_t::as_split_node()
{
    return nonstd::make_observer(dynamic_cast<split_node_t*>(this));
}

nonstd::observer_ptr<view_node_t> tree_node_t::as_view_node()
{
    return nonstd::make_observer(dynamic_cast<view_node_t*>(this));
}

wf::point_t get_wset_local_coordinates(std::shared_ptr<wf::workspace_set_t> wset, wf::point_t p)
{
    auto vp   = wset->get_current_workspace();
    auto size = wset->get_last_output_geometry().value_or(default_output_resolution);
    p.x -= vp.x * size.width;
    p.y -= vp.y * size.height;
    return p;
}

wf::geometry_t get_wset_local_coordinates(std::shared_ptr<wf::workspace_set_t> wset, wf::geometry_t g)
{
    auto new_tl = get_wset_local_coordinates(wset, wf::point_t{g.x, g.y});
    g.x = new_tl.x;
    g.y = new_tl.y;

    return g;
}

/* ---------------------- split_node_t implementation ----------------------- */
wf::geometry_t split_node_t::get_child_geometry(
    int32_t child_pos, int32_t child_size)
{
    wf::geometry_t child_geometry = this->geometry;
    switch (get_split_direction())
    {
      case SPLIT_HORIZONTAL:
        child_geometry.y += child_pos;
        child_geometry.height = child_size;
        break;

      case SPLIT_VERTICAL:
        child_geometry.x    += child_pos;
        child_geometry.width = child_size;
        break;
    }

    return child_geometry;
}

int32_t split_node_t::calculate_splittable(wf::geometry_t available) const
{
    switch (get_split_direction())
    {
      case SPLIT_HORIZONTAL:
        return available.height;

      case SPLIT_VERTICAL:
        return available.width;
    }

    return -1;
}

int32_t split_node_t::calculate_splittable() const
{
    return calculate_splittable(this->geometry);
}

void split_node_t::recalculate_children(wf::geometry_t available, wf::txn::transaction_uptr& tx)
{
    if (this->children.empty())
    {
        return;
    }

    set_gaps(this->gaps);

    /* Sway fraction model (sway/tree/arrange.c:15-52): children with a
     * fraction <= 0 are "fresh" — they take the average of the shares
     * currently occupied by the settled children (or 1.0 if there are no
     * settled children). Afterwards all fractions are normalized so that
     * they sum to 1.0. */
    int new_children = 0;
    double current = 0.0;
    for (auto& child : this->children)
    {
        current += child->fraction;
        if (child->fraction <= 0.0)
        {
            new_children += 1;
        }
    }

    double total = 0.0;
    for (auto& child : this->children)
    {
        if (child->fraction <= 0.0)
        {
            if (current <= 0.0)
            {
                child->fraction = 1.0;
            } else if (this->children.size() > (size_t)new_children)
            {
                child->fraction = current / (this->children.size() - new_children);
            } else
            {
                child->fraction = current;
            }
        }

        total += child->fraction;
    }

    for (auto& child : this->children)
    {
        child->fraction /= total;
    }

    const int32_t total_splittable = calculate_splittable(available);

    /* For each child, assign its share; the last child always uses the
     * remaining size so that rounding never leaves holes. */
    double up_to_now = 0.0;
    for (size_t i = 0; i < this->children.size(); i++)
    {
        auto& child = this->children[i];
        int32_t child_start = (int32_t)std::round(up_to_now * total_splittable);
        up_to_now += child->fraction;
        int32_t child_end = (i + 1 == this->children.size()) ?
            total_splittable :
            (int32_t)std::round(up_to_now * total_splittable);

        child->set_geometry(get_child_geometry(child_start, child_end - child_start), tx);
    }
}

void split_node_t::add_child(std::unique_ptr<tree_node_t> child, wf::txn::transaction_uptr& tx, int index)
{
    const int num_children = this->children.size();

    if ((index == -1) || (index > num_children))
    {
        index = num_children;
    }

    /* Add child to the list */
    child->parent = {this};

    // Set size of the child to make sure it gets properly recalculated later
    child->geometry = get_child_geometry(0, calculate_splittable());
    child->fraction = 0.0; // fresh child: average share on next arrange

    this->children.emplace(this->children.begin() + index, std::move(child));

    set_gaps(this->gaps);

    /* Recalculate geometry */
    recalculate_children(geometry, tx);
}

std::unique_ptr<tree_node_t> split_node_t::remove_child(
    nonstd::observer_ptr<tree_node_t> child, wf::txn::transaction_uptr& tx)
{
    /* Remove child */
    std::unique_ptr<tree_node_t> result;
    auto it = this->children.begin();

    while (it != this->children.end())
    {
        if (it->get() == child.get())
        {
            result = std::move(*it);
            it     = this->children.erase(it);
        } else
        {
            ++it;
        }
    }

    /* Remaining children have the full geometry */
    recalculate_children(this->geometry, tx);
    result->parent = nullptr;

    return result;
}

void split_node_t::set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr& tx)
{
    tree_node_t::set_geometry(geometry, tx);
    recalculate_children(geometry, tx);
}

void split_node_t::set_gaps(const gap_size_t& gaps)
{
    this->gaps = gaps;
    for (const auto& child : this->children)
    {
        gap_size_t child_gaps = gaps;

        /* See which edges are modified by this split */
        int32_t *first_edge, *second_edge;
        switch (this->split_direction)
        {
          case SPLIT_HORIZONTAL:
            first_edge  = &child_gaps.top;
            second_edge = &child_gaps.bottom;
            break;

          case SPLIT_VERTICAL:
            first_edge  = &child_gaps.left;
            second_edge = &child_gaps.right;
            break;

          default:
            assert(false);
        }

        /* Override internal edges */
        if (child != this->children.front())
        {
            *first_edge = gaps.internal;
        }

        if (child != this->children.back())
        {
            *second_edge = gaps.internal;
        }

        child->set_gaps(child_gaps);
    }
}

split_direction_t split_node_t::get_split_direction() const
{
    return this->split_direction;
}

void split_node_t::set_direction(split_direction_t direction)
{
    this->split_direction = direction;
}

split_node_t::split_node_t(split_direction_t dir)
{
    this->split_direction = dir;
    this->geometry = {0, 0, 0, 0};
}

/* -------------------- view_node_t implementation -------------------------- */
struct view_node_custom_data_t : public wf::custom_data_t
{
    nonstd::observer_ptr<view_node_t> ptr;
    view_node_custom_data_t(view_node_t *node)
    {
        ptr = nonstd::make_observer(node);
    }
};

view_node_t::view_node_t(wayfire_toplevel_view view)
{
    this->view = view;
    wf::dassert(!view->has_data<view_node_custom_data_t>(), "View already has custom data!");
    view->store_data(std::make_unique<view_node_custom_data_t>(this));
}

view_node_t::~view_node_t()
{
    view->erase_data<view_node_custom_data_t>();
}

void view_node_t::set_gaps(const gap_size_t& size)
{
    this->gaps = size;
}

wf::geometry_t view_node_t::calculate_target_geometry()
{
    /* Calculate view geometry in coordinates local to the active workspace,
     * because tree coordinates are kept in workspace-agnostic coordinates. */
    auto wset = view->get_wset();
    auto local_geometry = get_wset_local_coordinates(wset, geometry);

    local_geometry.x     += gaps.left;
    local_geometry.y     += gaps.top;
    local_geometry.width -= gaps.left + gaps.right;
    local_geometry.height -= gaps.top + gaps.bottom;

    auto size = wset->get_last_output_geometry().value_or(default_output_resolution);
    /* If view is maximized, we want to use the full available geometry */
    if (view->pending_fullscreen())
    {
        auto vp = wset->get_current_workspace();
        int view_vp_x = std::floor(1.0 * geometry.x / size.width);
        int view_vp_y = std::floor(1.0 * geometry.y / size.height);

        local_geometry = {
            (view_vp_x - vp.x) * size.width,
            (view_vp_y - vp.y) * size.height,
            size.width,
            size.height,
        };
    }

    if (view->sticky)
    {
        local_geometry.x = (local_geometry.x % size.width + size.width) % size.width;
        local_geometry.y = (local_geometry.y % size.height + size.height) % size.height;
    }

    return local_geometry;
}

void view_node_t::set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr& tx)
{
    tree_node_t::set_geometry(geometry, tx);

    if (!view->is_mapped())
    {
        return;
    }

    wf::get_core().default_wm->update_last_windowed_geometry(view);
    view->toplevel()->pending().tiled_edges = wf::TILED_EDGES_ALL;
    view->toplevel()->pending().geometry    = calculate_target_geometry();
    tx->add_object(view->toplevel());
}

wf::geometry_t view_node_t::target_geometry()
{
    return calculate_target_geometry();
}

nonstd::observer_ptr<view_node_t> view_node_t::get_node(wayfire_view view)
{
    if (!view->has_data<view_node_custom_data_t>())
    {
        return nullptr;
    }

    return view->get_data<view_node_custom_data_t>()->ptr;
}

/* ----------------- Generic tree operations implementation ----------------- */
bool flatten_tree(std::unique_ptr<tree_node_t>& root)
{
    /* Cannot flatten a view node */
    if (root->as_view_node())
    {
        return true;
    }

    for (auto it = root->children.begin(); it != root->children.end();)
    {
        if (!flatten_tree(*it))
        {
            it = root->children.erase(it);
        } else
        {
            ++it;
        }
    }

    if (root->children.empty())
    {
        return false;
    }

    /* No flattening required on this level */
    if (root->children.size() >= 2)
    {
        return true;
    }

    nonstd::observer_ptr<tree_node_t> child_ptr = {root->children.front()};

    /* A single view child => cannot make it root */
    if (child_ptr->as_view_node())
    {
        if (!root->parent)
        {
            return true;
        }
    }

    /* Rewire the tree, skipping the current root */
    child_ptr->parent = root->parent;
    root = std::move(root->children.front());
    return true;
}

nonstd::observer_ptr<split_node_t> get_root(
    nonstd::observer_ptr<tree_node_t> node)
{
    if (!node->parent)
    {
        return {dynamic_cast<split_node_t*>(node.get())};
    }

    return get_root(node->parent);
}

static void collect_divider_bands(const std::unique_ptr<tree_node_t>& node,
    std::vector<divider_band_t>& out, int tolerance)
{
    auto split = node->as_split_node();
    if (!split)
    {
        return;
    }

    for (size_t i = 0; i + 1 < split->children.size(); i++)
    {
        auto &first  = split->children[i];
        auto &second = split->children[i + 1];
        divider_band_t band;
        band.split     = {split};
        band.index     = i;
        band.direction = split->get_split_direction();

        if (band.direction == SPLIT_VERTICAL)
        {
            /* children side by side: the divider is a vertical strip */
            band.band.x      = first->geometry.x + first->geometry.width;
            band.band.width  = second->geometry.x - band.band.x;
            band.band.y      = split->geometry.y;
            band.band.height = split->geometry.height;
        } else
        {
            /* children stacked vertically: the divider is a horizontal
             * strip */
            band.band.y      = first->geometry.y + first->geometry.height;
            band.band.height = second->geometry.y - band.band.y;
            band.band.x      = split->geometry.x;
            band.band.width  = split->geometry.width;
        }

        /* widen along the split axis so narrow gaps are grabbable */
        if (band.direction == SPLIT_VERTICAL)
        {
            band.band.x      -= tolerance;
            band.band.width  += 2 * tolerance;
        } else
        {
            band.band.y      -= tolerance;
            band.band.height += 2 * tolerance;
        }

        out.push_back(band);
    }

    for (auto& child : split->children)
    {
        collect_divider_bands(child, out, tolerance);
    }
}

std::vector<divider_band_t> get_divider_bands(
    const std::unique_ptr<tree_node_t>& root, int tolerance)
{
    std::vector<divider_band_t> out;
    collect_divider_bands(root, out, tolerance);
    return out;
}
}
