/* SPDX-License-Identifier: GPL-2.0-only */
/* Derived from Wayfire plugins/tile/tile-wset.hpp (GPL-2.0-only),
 * (c) Wayfire developers; modified for layout-manager:
 *   - root split direction is chosen per workspace root on first creation
 *     from layout-manager/default_orientation (auto follows sway
 *     output.c:441-450: workarea taller than wide -> vertical layout)
 *   - grid resize preserves existing roots (only grows/shrinks the grid)
 *   - new views are inserted after the last focused tiled view (sway
 *     sibling-insertion semantics) instead of being appended
 *   - gap options are layout-manager/gaps_inner + gaps_outer */
#pragma once

#include "lm-tree.hpp"
#include "wayfire/view-helpers.hpp"
#include "wayfire/txn/transaction-manager.hpp"
#include "wayfire/scene-operations.hpp"
#include <wayfire/workarea.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/option-wrapper.hpp>

namespace lm
{
struct autocommit_transaction_t
{
  public:
    wf::txn::transaction_uptr tx;
    autocommit_transaction_t()
    {
        tx = wf::txn::transaction_t::create();
    }

    ~autocommit_transaction_t()
    {
        if (!tx->get_objects().empty())
        {
            wf::get_core().tx_manager->schedule_transaction(std::move(tx));
        }
    }
};

/**
 * Marker: a view is being moved to another wset and its tiled status should
 * be preserved (re-attached on arrival). Same role as Wayfire tile's
 * view_auto_tile_t.
 */
class lm_keep_state_t : public wf::custom_data_t
{};

/**
 * Decide the split direction of a workspace root on first creation.
 *
 * Tile convention: SPLIT_HORIZONTAL = children stacked vertically
 * (sway L_VERT), SPLIT_VERTICAL = children side by side (sway L_HORIZ).
 */
inline lm::split_direction_t choose_root_direction(wf::geometry_t area)
{
    wf::option_wrapper_t<std::string> orient{"layout-manager/default_orientation"};
    std::string v = orient.value();
    if (v == "h")
    {
        return SPLIT_VERTICAL; // children side by side
    }

    if (v == "v")
    {
        return SPLIT_HORIZONTAL; // children stacked vertically
    }

    /* auto: sway/tree/output.c:441-450 */
    return (area.height > area.width) ? SPLIT_HORIZONTAL : SPLIT_VERTICAL;
}

class lm_workspace_set_data_t : public wf::custom_data_t
{
  public:
    std::vector<std::vector<std::unique_ptr<lm::tree_node_t>>> roots;
    std::vector<std::vector<wf::scene::floating_inner_ptr>> tiled_sublayer;

    wf::option_wrapper_t<int> inner_gaps{"layout-manager/gaps_inner"};
    wf::option_wrapper_t<int> outer_gaps{"layout-manager/gaps_outer"};

    /**
     * The last focused tiled view of this wset; new views are inserted as
     * its sibling (sway semantics). Kept as a view handle so that the node
     * can be re-derived (and validated) at insertion time.
     */
    wayfire_toplevel_view last_tiled_view = nullptr;

    lm_workspace_set_data_t(std::shared_ptr<wf::workspace_set_t> wset)
    {
        this->wset = wset;
        wset->connect(&on_wset_attached);
        wset->connect(&on_workspace_grid_changed);
        resize_roots(wset->get_workspace_grid_size());
        if (wset->get_attached_output())
        {
            wset->get_attached_output()->connect(&on_workarea_changed);
        }

        inner_gaps.set_callback(update_gaps);
        outer_gaps.set_callback(update_gaps);
    }

    ~lm_workspace_set_data_t()
    {
        for (auto& row : roots)
        {
            for (auto& root : row)
            {
                if (root)
                {
                    lm::for_each_view(root, [] (wayfire_toplevel_view view)
                    {
                        view->set_allowed_actions(wf::VIEW_ALLOW_ALL);
                    });
                }
            }
        }
    }

    wf::signal::connection_t<wf::workarea_changed_signal> on_workarea_changed = [=] (auto)
    {
        update_root_size();
    };

    wf::signal::connection_t<wf::workspace_set_attached_signal> on_wset_attached = [=] (auto)
    {
        on_workarea_changed.disconnect();
        if (wset.lock()->get_attached_output())
        {
            wset.lock()->get_attached_output()->connect(&on_workarea_changed);
            update_root_size();
        }
    };

    wf::signal::connection_t<wf::workspace_grid_changed_signal> on_workspace_grid_changed = [=] (auto)
    {
        wf::dassert(!wset.expired(), "wset should not expire, ever!");
        resize_roots(wset.lock()->get_workspace_grid_size());
    };

    void resize_roots(wf::dimensions_t wsize)
    {
        /* Unlike Wayfire tile (which recreates all roots and therefore
         * drops all trees), we keep the trees of the workspaces that remain
         * valid, and only create roots for new workspaces. */
        for (size_t i = 0; i < roots.size(); i++)
        {
            for (size_t j = 0; j < roots[i].size(); j++)
            {
                if (roots[i][j] &&
                    !wset.lock()->is_workspace_valid({(int)i, (int)j}))
                {
                    /* workspace disappeared: untile its views, drop the
                     * tree and the sublayer */
                    std::vector<nonstd::observer_ptr<lm::view_node_t>> nodes;
                    lm::for_each_view(roots[i][j], [&] (wayfire_toplevel_view view)
                    {
                        if (auto n = lm::view_node_t::get_node(view))
                        {
                            nodes.push_back(n);
                        }
                    });
                    detach_views(nodes);
                    roots[i][j]    = nullptr;
                    tiled_sublayer[i][j] = nullptr;
                }
            }
        }

        roots.resize(wsize.width);
        tiled_sublayer.resize(wsize.width);
        for (int i = 0; i < wsize.width; i++)
        {
            roots[i].resize(wsize.height);
            tiled_sublayer[i].resize(wsize.height);
            for (int j = 0; j < wsize.height; j++)
            {
                if (!roots[i][j])
                {
                    roots[i][j] = std::make_unique<lm::split_node_t>(
                        choose_root_direction(workarea_or_default()));
                    tiled_sublayer[i][j] =
                        std::make_shared<wf::scene::floating_inner_node_t>(false);
                    wf::scene::add_front(wset.lock()->get_node(),
                        tiled_sublayer[i][j]);
                }
            }
        }

        update_root_size();
        update_gaps();
    }

    void update_root_size()
    {
        auto wo = wset.lock()->get_attached_output();
        wf::geometry_t workarea = workarea_or_default();

        wf::geometry_t output_geometry = wset.lock()->get_last_output_geometry().value_or(
            lm::default_output_resolution);

        auto wsize = wset.lock()->get_workspace_grid_size();
        for (int i = 0; i < wsize.width; i++)
        {
            for (int j = 0; j < wsize.height; j++)
            {
                if (!roots[i][j])
                {
                    continue;
                }

                /* Set size */
                auto vp_geometry = workarea;
                vp_geometry.x += i * output_geometry.width;
                vp_geometry.y += j * output_geometry.height;

                autocommit_transaction_t tx;
                roots[i][j]->set_geometry(vp_geometry, tx.tx);
            }
        }
    }

    void destroy_sublayer(wf::scene::floating_inner_ptr sublayer)
    {
        // Transfer views to the top
        auto root     = wset.lock()->get_node();
        auto children = root->get_children();
        auto sublayer_children = sublayer->get_children();
        sublayer->set_children_list({});
        children.insert(children.end(), sublayer_children.begin(), sublayer_children.end());
        root->set_children_list(children);
        wf::scene::update(root, wf::scene::update_flag::CHILDREN_LIST);
        wf::scene::remove_child(sublayer);
    }

    lm::gap_size_t get_gaps() const
    {
        return {
            .left     = outer_gaps,
            .right    = outer_gaps,
            .top      = outer_gaps,
            .bottom   = outer_gaps,
            .internal = inner_gaps,
        };
    }

    void update_gaps_with_tx(wf::txn::transaction_uptr& tx)
    {
        for (auto& col : roots)
        {
            for (auto& root : col)
            {
                if (!root)
                {
                    continue;
                }

                root->set_gaps(get_gaps());
                root->set_geometry(root->geometry, tx);
            }
        }
    }

    void refresh(wf::txn::transaction_uptr& tx)
    {
        flatten_roots();
        update_gaps_with_tx(tx);
    }

    std::function<void()> update_gaps = [=] ()
    {
        autocommit_transaction_t tx;
        update_gaps_with_tx(tx.tx);
    };

    void flatten_roots()
    {
        for (auto& col : roots)
        {
            for (auto& root : col)
            {
                if (root)
                {
                    lm::flatten_tree(root);
                }
            }
        }
    }

    static lm_workspace_set_data_t& get(std::shared_ptr<wf::workspace_set_t> set)
    {
        if (!set->has_data<lm_workspace_set_data_t>())
        {
            set->store_data(std::make_unique<lm_workspace_set_data_t>(set));
        }

        return *set->get_data<lm_workspace_set_data_t>();
    }

    static lm_workspace_set_data_t& get(wf::output_t *output)
    {
        return get(output->wset());
    }

    static std::unique_ptr<lm::tree_node_t>& get_current_root(wf::output_t *output)
    {
        auto set   = output->wset();
        auto vp    = set->get_current_workspace();
        auto& data = get(output);
        return data.roots[vp.x][vp.y];
    }

    static wf::scene::floating_inner_ptr get_current_sublayer(wf::output_t *output)
    {
        auto set   = output->wset();
        auto vp    = set->get_current_workspace();
        auto& data = get(output);
        return data.tiled_sublayer[vp.x][vp.y];
    }

    std::weak_ptr<wf::workspace_set_t> wset;

    /**
     * The node after which a newly attached view should be inserted, or
     * nullptr: the last focused tiled view of this wset, if it is still
     * part of the tree of the given workspace root.
     */
    nonstd::observer_ptr<lm::tree_node_t> insertion_reference(
        std::unique_ptr<lm::tree_node_t>& root)
    {
        if (!last_tiled_view)
        {
            return nullptr;
        }

        auto node = lm::view_node_t::get_node(last_tiled_view);
        if (!node || !node->parent)
        {
            return nullptr;
        }

        /* The reference must belong to the tree we are inserting into */
        if (lm::get_root(node) !=
            root->as_split_node())
        {
            return nullptr;
        }

        return node;
    }

    std::unique_ptr<lm::view_node_t> setup_view_tiling(wayfire_toplevel_view view, wf::point_t vp)
    {
        view->set_allowed_actions(wf::VIEW_ALLOW_WS_CHANGE);
        auto node = view->get_root_node();
        wf::scene::readd_front(tiled_sublayer[vp.x][vp.y], node);
        view_bring_to_front(view);
        return std::make_unique<lm::view_node_t>(view);
    }

    /**
     * Attach a view to the tiling tree of the given workspace.
     *
     * @param after Insert the view as sibling after this node (sway
     *   insertion semantics). When null, the last focused tiled view is
     *   used; when neither is available, the view is appended to the root.
     */
    void attach_view(wayfire_toplevel_view view, std::optional<wf::point_t> _vp = {},
        nonstd::observer_ptr<lm::tree_node_t> after = nullptr)
    {
        auto vp = _vp.value_or(wset.lock()->get_current_workspace());
        if (!wset.lock()->is_workspace_valid(vp))
        {
            vp = wset.lock()->get_current_workspace();
        }

        auto view_node = setup_view_tiling(view, vp);
        auto& root     = roots[vp.x][vp.y];
        auto ref = after ? after : insertion_reference(root);
        if (ref && ref->parent)
        {
            /* insert as the next sibling of the reference */
            int index = 0;
            for (size_t i = 0; i < ref->parent->children.size(); i++)
            {
                if (ref->parent->children[i].get() == ref.get())
                {
                    index = i + 1;
                    break;
                }
            }

            autocommit_transaction_t tx;
            ref->parent->add_child(std::move(view_node), tx.tx, index);
        } else
        {
            autocommit_transaction_t tx;
            root->as_split_node()->add_child(std::move(view_node), tx.tx);
        }

        last_tiled_view = view;
        consider_exit_fullscreen(view);
    }

    /** Remove the given views from their tiling container */
    void detach_views(std::vector<nonstd::observer_ptr<lm::view_node_t>> views,
        bool reinsert = true)
    {
        {
            autocommit_transaction_t tx;
            std::vector<nonstd::observer_ptr<lm::split_node_t>> affected;
            for (auto& v : views)
            {
                auto view   = v->view;
                auto parent = v->parent;
                view->set_allowed_actions(wf::VIEW_ALLOW_ALL);
                if (last_tiled_view == view)
                {
                    last_tiled_view = nullptr;
                }

                /* Remove the node from the tree without triggering the
                 * per-removal re-arrange: the re-arrange must happen once,
                 * after all removals — intermediate recalculations would
                 * write tiled geometries of the not-yet-removed views into
                 * this same transaction and overwrite a concurrent restore
                 * pass (mode switch back to stacking). */
                auto &siblings = parent->children;
                for (auto it = siblings.begin(); it != siblings.end(); ++it)
                {
                    if (it->get() == v.get())
                    {
                        siblings.erase(it);
                        break;
                    }
                }

                affected.push_back(parent);

                if (view->pending_fullscreen() && view->is_mapped())
                {
                    wf::get_core().default_wm->fullscreen_request(view, nullptr, false);
                }

                if (reinsert && view->get_output())
                {
                    wf::scene::readd_front(view->get_output()->wset()->get_node(),
                        view->get_root_node());
                }
            }

            /* Re-arrange every affected split exactly once */
            for (auto& p : affected)
            {
                p->set_geometry(p->geometry, tx.tx);
            }
        }

        /* View nodes are invalid now */
        flatten_roots();
        update_root_size();
    }

    /**
     * Consider unfullscreening all fullscreen views because a new view has been focused or attached to the
     * tiling tree.
     */
    void consider_exit_fullscreen(wayfire_toplevel_view view)
    {
        if (lm::view_node_t::get_node(view) && !view->pending_fullscreen())
        {
            auto vp = this->wset.lock()->get_current_workspace();
            for_each_view(roots[vp.x][vp.y], [&] (wayfire_toplevel_view view)
            {
                if (view->pending_fullscreen())
                {
                    set_view_fullscreen(view, false);
                }
            });
        }
    }

    void set_view_fullscreen(wayfire_toplevel_view view, bool fullscreen)
    {
        /* Set fullscreen, and trigger resizing of the views (which will commit the view) */
        view->toplevel()->pending().fullscreen = fullscreen;
        update_root_size();
    }

  private:
    wf::geometry_t workarea_or_default() const
    {
        auto wo = wset.lock()->get_attached_output();
        return wo ? wo->workarea->get_workarea() : lm::default_output_resolution;
    }
};
}
