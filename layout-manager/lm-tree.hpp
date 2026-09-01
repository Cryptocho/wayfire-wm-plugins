/* SPDX-License-Identifier: GPL-2.0-only */
/* Derived from Wayfire plugins/tile/tree.hpp (GPL-2.0-only),
 * (c) Wayfire developers; modified for layout-manager:
 *   - fraction-based arrange (sway/tree/arrange.c semantics)
 *   - no crossfade/scale animations (geometry is committed directly)
 *   - per-node fraction field for divider drag adjustments. */
#ifndef LM_TREE
#define LM_TREE

#include "wayfire/signal-definitions.hpp"
#include "wayfire/workspace-set.hpp"
#include <wayfire/view.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/txn/transaction.hpp>

namespace lm
{
/**
 * A tree node represents a logical container of views in the tiled part of
 * a workspace.
 *
 * There are two types of nodes:
 * 1. View tree nodes, i.e leaves, they contain a single view
 * 2. Split tree nodes, they contain at least 1 child view.
 */
struct split_node_t;
struct view_node_t;

struct gap_size_t
{
    /* Gap on the left side */
    int32_t left = 0;
    /* Gap on the right side */
    int32_t right = 0;
    /* Gap on the top side */
    int32_t top = 0;
    /* Gap on the bottom side */
    int32_t bottom = 0;
    /* Gap for internal splits */
    int32_t internal = 0;
};

struct tree_node_t
{
    /** The node parent, or nullptr if this is the root node */
    nonstd::observer_ptr<split_node_t> parent;

    /** The children of the node */
    std::vector<std::unique_ptr<tree_node_t>> children;

    /** The geometry occupied by the node */
    wf::geometry_t geometry;

    /**
     * Share of this node within its parent split (sway width/height
     * fraction). 0 means "fresh child" — it gets assigned a share on the
     * next arrange. Only meaningful when parent != nullptr.
     */
    double fraction = 0.0;

    /** Set the geometry available for the node and its subnodes. */
    virtual void set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr& tx);

    /** Set the gaps for the node and subnodes. */
    virtual void set_gaps(const gap_size_t& gaps) = 0;

    gap_size_t get_gaps() const
    {
        return gaps;
    }

    virtual ~tree_node_t()
    {}

    /** Simply dynamic cast this to a split_node_t */
    nonstd::observer_ptr<split_node_t> as_split_node();
    /** Simply dynamic cast this to a view_node_t */
    nonstd::observer_ptr<view_node_t> as_view_node();

  protected:
    /* Gaps */
    gap_size_t gaps;
};

/**
 * A node which contains a split can be split either horizontally or vertically
 */
enum split_direction_t
{
    SPLIT_HORIZONTAL = 0,
    SPLIT_VERTICAL   = 1,
};

/*
 * Represents a node in the tree which contains at 1 one child node
 */
struct split_node_t : public tree_node_t
{
    /**
     * Add the given child to the list of children.
     *
     * The new child is a "fresh" child (fraction = 0) and takes the average
     * share of the existing children on the next arrange (sway semantics).
     *
     * @param index The index at which to insert the new child, or -1 for
     *              adding to the end of the child list.
     */
    void add_child(std::unique_ptr<tree_node_t> child, wf::txn::transaction_uptr& tx, int index = -1);

    /**
     * Remove a child from the node, and return its unique_ptr
     */
    std::unique_ptr<tree_node_t> remove_child(
        nonstd::observer_ptr<tree_node_t> child, wf::txn::transaction_uptr& tx);

    /**
     * Set the total geometry available to the node. This will recursively
     * resize the children nodes according to their fractions.
     */
    void set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr& tx) override;

    /**
     * Set the gaps for the subnodes. The internal gap will override
     * the corresponding edges for each child.
     */
    void set_gaps(const gap_size_t& gaps) override;

    split_node_t(split_direction_t direction);
    split_direction_t get_split_direction() const;

    /** Change the split direction in place (sway single-child split
     *  optimization); re-arranges nothing by itself. */
    void set_direction(split_direction_t direction);

  private:
    split_direction_t split_direction;

    /**
     * Resize the children so that they fit inside the given
     * available_geometry, according to their fractions.
     */
    void recalculate_children(wf::geometry_t available_geometry, wf::txn::transaction_uptr& tx);

    /**
     * Calculate the geometry of a child if it has child_size as one
     * dimension. Whether this is width/height depends on the node split type.
     *
     * @param child_pos The position from which the child starts, relative to
     *                  the node itself
     *
     * @return The geometry of the child, in global coordinates
     */
    wf::geometry_t get_child_geometry(int32_t child_pos, int32_t child_size);

    /** Return the size of the node in the dimension in which the split happens */
    int32_t calculate_splittable() const;
    /** Return the size of the geometry in the dimension in which the split
     * happens */
    int32_t calculate_splittable(wf::geometry_t geometry) const;
};

/**
 * Represents a leaf in the tree, contains a single view
 */
struct view_node_t : public tree_node_t
{
    view_node_t(wayfire_toplevel_view view);
    ~view_node_t();

    wayfire_toplevel_view view;

    /**
     * Set the geometry of the node and the contained view.
     *
     * Note that the resulting view geometry will not always be equal to the
     * geometry of the node. For example, a fullscreen view will always have
     * the geometry of the whole output.
     */
    void set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr& tx) override;

    /**
     * Set the gaps for non-fullscreen mode.
     * The gap sizes will be subtracted from all edges of the view's geometry.
     */
    void set_gaps(const gap_size_t& gaps) override;

    /* Return the tree node corresponding to the view, or nullptr if none */
    static nonstd::observer_ptr<view_node_t> get_node(wayfire_view view);

    /** The geometry the view will be placed at (wset-local, gaps applied) */
    wf::geometry_t target_geometry();

  private:
    wf::geometry_t calculate_target_geometry();
};

/**
 * Flatten the tree as much as possible, i.e remove nodes with only one
 * split-node child.
 *
 * The only exception is "the root", which will always be a split node.
 *
 * Note: this will potentially invalidate pointers to the tree and modify
 * the given parameter.
 *
 * @return True if the tree has any views in it.
 */
bool flatten_tree(std::unique_ptr<tree_node_t>& root);

/**
 * Get the root of the tree which node is part of
 */
nonstd::observer_ptr<split_node_t> get_root(nonstd::observer_ptr<tree_node_t> node);

/**
 * A draggable divider band between two adjacent children of a split node,
 * in wset-global coordinates. The band rectangle is widened by `tolerance`
 * along the split axis so that narrow gaps are grabbable.
 */
struct divider_band_t
{
    nonstd::observer_ptr<split_node_t> split;
    /** Index of the left/top child; dragging moves the boundary between
     *  children[index] and children[index + 1]. */
    int index;
    wf::geometry_t band;
    split_direction_t direction;
};

/**
 * Enumerate the divider bands of the tree (between adjacent children of
 * every split node).
 */
std::vector<divider_band_t> get_divider_bands(
    const std::unique_ptr<tree_node_t>& root, int tolerance = 4);


/**
 * Call the given function on every view in the tree.
 */
template<class Pred>
void for_each_view(std::unique_ptr<tree_node_t>& root, Pred&& callback)
{
    if (root->as_view_node())
    {
        callback(root->as_view_node()->view);
        return;
    }

    for (auto& child : root->children)
    {
        for_each_view(child, callback);
    }
}

/**
 * Transform coordinates from the tiling trees coordinate system to wset-local coordinates.
 */
wf::geometry_t get_wset_local_coordinates(std::shared_ptr<wf::workspace_set_t> wset, wf::geometry_t g);
wf::point_t get_wset_local_coordinates(std::shared_ptr<wf::workspace_set_t> wset, wf::point_t g);

// Since wsets may not have been attached to any output yet, they may not have a native 'resolution'.
// In this case, we use a default resolution of 1920x1080 in order to layout views. This resolution will be
// automatically adjusted once the wset is added to an output.
static constexpr wf::geometry_t default_output_resolution = {0, 0, 1920, 1080};
}

#endif /* end of guard: LM_TREE */
