/* SPDX-License-Identifier: GPL-2.0-only */
/* Derived from Wayfire plugins/tile/tile-ipc.hpp (GPL-2.0-only),
 * (c) Wayfire developers; modified for layout-manager: only the
 * layout-inspection side (tree -> JSON); set-layout is out of P3 scope. */
#pragma once

#include <wayfire/workspace-set.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include "lm-tree.hpp"
#include "lm-wset.hpp"

namespace lm
{
/**
 * Get a json description of the given tiling tree.
 */
inline wf::json_t tree_to_json(const std::unique_ptr<tree_node_t>& root,
    const wf::point_t& offset,
    double rel_size = 1.0)
{
    wf::json_t js;
    js["percent"]  = rel_size;
    if (auto view = root->as_view_node())
    {
        /* Report the actual target geometry of the view (wset-local,
         * gaps applied), not the raw node box. */
        js["geometry"] = wf::ipc::geometry_to_json(view->target_geometry());
        js["view-id"]  = view->view->get_id();
        return js;
    }

    auto split = root->as_split_node();
    wf::dassert(split != nullptr, "Expected to be split node");
    js["geometry"] = wf::ipc::geometry_to_json(root->geometry - offset);

    wf::json_t children = wf::json_t::array();
    if (split->get_split_direction() == SPLIT_HORIZONTAL)
    {
        for (auto& child : split->children)
        {
            children.append(
                tree_to_json(child, offset, 1.0 * child->geometry.height / split->geometry.height));
        }

        js["horizontal-split"] = std::move(children);
    } else
    {
        for (auto& child : split->children)
        {
            children.append(tree_to_json(
                child, offset, 1.0 * child->geometry.width / split->geometry.width));
        }

        js["vertical-split"] = std::move(children);
    }

    return js;
}

/**
 * layout-manager/get-layout: JSON dump of the tiling tree of the active
 * output's workspace set. Optional "workspace": {"x", "y"} selects another
 * workspace of the same grid.
 */
inline wf::json_t handle_ipc_get_layout(const wf::json_t& params)
{
    auto output = wf::get_core().seat->get_active_output();
    if (!output)
    {
        return wf::ipc::json_error("no active output");
    }

    auto ws = output->wset();
    auto vp = ws->get_current_workspace();
    if (params.has_member("workspace"))
    {
        vp.x = wf::ipc::json_get_int64(params["workspace"], "x");
        vp.y = wf::ipc::json_get_int64(params["workspace"], "y");
    }

    auto grid_size = ws->get_workspace_grid_size();
    if ((vp.x >= grid_size.width) || (vp.y >= grid_size.height) ||
        (vp.x < 0) || (vp.y < 0))
    {
        return wf::ipc::json_error("invalid workspace coordinates");
    }

    auto response = wf::ipc::json_ok();
    auto resolution = ws->get_last_output_geometry().value_or(
        lm::default_output_resolution);
    wf::point_t offset = {vp.x * resolution.width, vp.y * resolution.height};

    auto& data = lm_workspace_set_data_t::get(ws);
    response["layout"] = tree_to_json(data.roots[vp.x][vp.y], offset);
    return response;
}
}
