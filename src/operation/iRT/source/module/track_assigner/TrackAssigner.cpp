// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "TrackAssigner.hpp"

#include "DRCEngine.hpp"
#include "GDSPlotter.hpp"
#include "LayerCoord.hpp"
#include "Monitor.hpp"
#include "RTInterface.hpp"
#include "TAPanel.hpp"

namespace irt {

// public

void TrackAssigner::initInst()
{
  if (_ta_instance == nullptr) {
    _ta_instance = new TrackAssigner();
  }
}

TrackAssigner& TrackAssigner::getInst()
{
  if (_ta_instance == nullptr) {
    RTLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_ta_instance;
}

void TrackAssigner::destroyInst()
{
  if (_ta_instance != nullptr) {
    delete _ta_instance;
    _ta_instance = nullptr;
  }
}

// function

void TrackAssigner::assign()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");
  TAModel ta_model = initTAModel();
  // debugPlotTAModel(ta_model, "before");
  setTAParameter(ta_model);
  initTAPanelMap(ta_model);
  buildPanelSchedule(ta_model);
  assignTAPanelMap(ta_model);
  // debugPlotTAModel(ta_model, "after");
  updateSummary(ta_model);
  printSummary(ta_model);
  writeNetCSV(ta_model);
  writeViolationCSV(ta_model);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

TrackAssigner* TrackAssigner::_ta_instance = nullptr;

TAModel TrackAssigner::initTAModel()
{
  std::vector<Net>& net_list = RTDM.getDatabase().get_net_list();

  TAModel ta_model;
  ta_model.set_ta_net_list(convertToTANetList(net_list));
  return ta_model;
}

std::vector<TANet> TrackAssigner::convertToTANetList(std::vector<Net>& net_list)
{
  std::vector<TANet> ta_net_list;
  ta_net_list.reserve(net_list.size());
  for (size_t i = 0; i < net_list.size(); i++) {
    ta_net_list.emplace_back(convertToTANet(net_list[i]));
  }
  return ta_net_list;
}

TANet TrackAssigner::convertToTANet(Net& net)
{
  TANet ta_net;
  ta_net.set_origin_net(&net);
  ta_net.set_net_idx(net.get_net_idx());
  ta_net.set_connect_type(net.get_connect_type());
  for (Pin& pin : net.get_pin_list()) {
    ta_net.get_ta_pin_list().push_back(TAPin(pin));
  }
  return ta_net;
}

void TrackAssigner::setTAParameter(TAModel& ta_model)
{
  int32_t cost_unit = 8;
  TAParameter ta_parameter(128 * cost_unit, 32 * cost_unit, 32 * cost_unit, 4);
  RTLOG.info(Loc::current(), "prefer_wire_unit: ", ta_parameter.get_prefer_wire_unit());
  RTLOG.info(Loc::current(), "corner_unit: ", ta_parameter.get_corner_unit());
  RTLOG.info(Loc::current(), "fixed_rect_unit: ", ta_parameter.get_fixed_rect_unit());
  RTLOG.info(Loc::current(), "routed_rect_unit: ", ta_parameter.get_routed_rect_unit());
  RTLOG.info(Loc::current(), "violation_unit: ", ta_parameter.get_violation_unit());
  RTLOG.info(Loc::current(), "max_routed_times: ", ta_parameter.get_max_routed_times());
  ta_model.set_ta_parameter(ta_parameter);
}

void TrackAssigner::initTAPanelMap(TAModel& ta_model)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  Die& die = RTDM.getDatabase().get_die();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  TAParameter& ta_parameter = ta_model.get_ta_parameter();
  std::vector<std::vector<TAPanel>>& layer_panel_list = ta_model.get_layer_panel_list();
  for (RoutingLayer& routing_layer : routing_layer_list) {
    std::vector<TAPanel> ta_panel_list;
    if (routing_layer.isPreferH()) {
      for (ScaleGrid& gcell_grid : gcell_axis.get_y_grid_list()) {
        for (int32_t line = gcell_grid.get_start_line(); line < gcell_grid.get_end_line(); line += gcell_grid.get_step_length()) {
          TAPanel ta_panel;
          EXTLayerRect ta_panel_rect;
          ta_panel_rect.set_real_ll(die.get_real_ll_x(), line);
          ta_panel_rect.set_real_ur(die.get_real_ur_x(), line + gcell_grid.get_step_length());
          ta_panel_rect.set_grid_rect(RTUTIL.getOpenGCellGridRect(ta_panel_rect.get_real_rect(), gcell_axis));
          ta_panel_rect.set_layer_idx(routing_layer.get_layer_idx());
          ta_panel.set_panel_rect(ta_panel_rect);
          TAPanelId ta_panel_id;
          ta_panel_id.set_layer_idx(routing_layer.get_layer_idx());
          ta_panel_id.set_panel_idx(static_cast<int32_t>(ta_panel_list.size()));
          ta_panel.set_ta_panel_id(ta_panel_id);
          ta_panel.set_ta_parameter(&ta_parameter);
          ta_panel_list.push_back(ta_panel);
        }
      }
    } else {
      for (ScaleGrid& gcell_grid : gcell_axis.get_x_grid_list()) {
        for (int32_t line = gcell_grid.get_start_line(); line < gcell_grid.get_end_line(); line += gcell_grid.get_step_length()) {
          TAPanel ta_panel;
          EXTLayerRect ta_panel_rect;
          ta_panel_rect.set_real_ll(line, die.get_real_ll_y());
          ta_panel_rect.set_real_ur(line + gcell_grid.get_step_length(), die.get_real_ur_y());
          ta_panel_rect.set_grid_rect(RTUTIL.getOpenGCellGridRect(ta_panel_rect.get_real_rect(), gcell_axis));
          ta_panel_rect.set_layer_idx(routing_layer.get_layer_idx());
          ta_panel.set_panel_rect(ta_panel_rect);
          TAPanelId ta_panel_id;
          ta_panel_id.set_layer_idx(routing_layer.get_layer_idx());
          ta_panel_id.set_panel_idx(static_cast<int32_t>(ta_panel_list.size()));
          ta_panel.set_ta_panel_id(ta_panel_id);
          ta_panel.set_ta_parameter(&ta_parameter);
          ta_panel_list.push_back(ta_panel);
        }
      }
    }
    layer_panel_list.push_back(ta_panel_list);
  }
}

void TrackAssigner::buildPanelSchedule(TAModel& ta_model)
{
  std::vector<std::vector<TAPanel>>& layer_panel_list = ta_model.get_layer_panel_list();

  int32_t range = 2;

  std::vector<std::vector<TAPanelId>> ta_panel_id_list_list;
  for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(layer_panel_list.size()); layer_idx++) {
    for (int32_t start_i = 0; start_i < range; start_i++) {
      std::vector<TAPanelId> ta_panel_id_list;
      for (int32_t i = start_i; i < static_cast<int32_t>(layer_panel_list[layer_idx].size()); i += range) {
        ta_panel_id_list.emplace_back(layer_idx, i);
      }
      ta_panel_id_list_list.push_back(ta_panel_id_list);
    }
  }
  ta_model.set_ta_panel_id_list_list(ta_panel_id_list_list);
}

void TrackAssigner::assignTAPanelMap(TAModel& ta_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<std::vector<TAPanel>>& layer_panel_list = ta_model.get_layer_panel_list();

  size_t total_panel_num = 0;
  for (std::vector<TAPanelId>& ta_panel_id_list : ta_model.get_ta_panel_id_list_list()) {
    total_panel_num += ta_panel_id_list.size();
  }

  size_t assigned_panel_num = 0;
  for (std::vector<TAPanelId>& ta_panel_id_list : ta_model.get_ta_panel_id_list_list()) {
    Monitor stage_monitor;
#pragma omp parallel for
    for (TAPanelId& ta_panel_id : ta_panel_id_list) {
      TAPanel& ta_panel = layer_panel_list[ta_panel_id.get_layer_idx()][ta_panel_id.get_panel_idx()];
      buildFixedRect(ta_panel);
      buildAccessResult(ta_panel);
      initTATaskList(ta_model, ta_panel);
      if (needRouting(ta_panel)) {
        buildPanelTrackAxis(ta_panel);
        buildTANodeMap(ta_panel);
        buildTANodeNeighbor(ta_panel);
        buildOrientNetMap(ta_panel);
        // debugCheckTAPanel(ta_panel);
        // debugPlotTAPanel(ta_panel, -1, "before");
        routeTAPanel(ta_panel);
        // debugPlotTAPanel(ta_panel, -1, "after");
        uploadNetResult(ta_panel);
        uploadViolation(ta_panel);
      }
      freeTAPanel(ta_panel);
    }
    assigned_panel_num += ta_panel_id_list.size();
    RTLOG.info(Loc::current(), "Assigned ", assigned_panel_num, "/", total_panel_num, "(",
               RTUTIL.getPercentage(assigned_panel_num, total_panel_num), ") panels with ", getViolationNum(), " violations",
               stage_monitor.getStatsInfo());
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void TrackAssigner::buildFixedRect(TAPanel& ta_panel)
{
  ta_panel.set_type_layer_net_fixed_rect_map(RTDM.getTypeLayerNetFixedRectMap(ta_panel.get_panel_rect()));
}

void TrackAssigner::buildAccessResult(TAPanel& ta_panel)
{
  ta_panel.set_net_access_result_map(RTDM.getNetAccessResultMap(ta_panel.get_panel_rect()));
}

void TrackAssigner::initTATaskList(TAModel& ta_model, TAPanel& ta_panel)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  std::vector<TANet>& ta_net_list = ta_model.get_ta_net_list();
  std::vector<TATask*>& ta_task_list = ta_panel.get_ta_task_list();

  for (auto& [net_idx, segment_set] : RTDM.getGlobalNetResultMap(ta_panel.get_panel_rect())) {
    TANet& ta_net = ta_net_list[net_idx];
    for (Segment<LayerCoord>* segment : segment_set) {
      LayerCoord& first_coord = segment->get_first();
      LayerCoord& second_coord = segment->get_second();
      if (first_coord.get_layer_idx() != second_coord.get_layer_idx()) {
        continue;
      }
      if (first_coord.get_layer_idx() != ta_panel.get_ta_panel_id().get_layer_idx()) {
        continue;
      }
      PlanarRect ll_rect = RTUTIL.getRealRectByGCell(first_coord, gcell_axis);
      PlanarRect ur_rect = RTUTIL.getRealRectByGCell(second_coord, gcell_axis);
      int32_t layer_idx = first_coord.get_layer_idx();

      RoutingLayer& routing_layer = routing_layer_list[layer_idx];
      std::vector<ScaleGrid>& x_track_grid_list = routing_layer.getXTrackGridList();
      std::vector<ScaleGrid>& y_track_grid_list = routing_layer.getYTrackGridList();

      std::vector<TAGroup> ta_group_list(2);
      if (RTUTIL.isHorizontal(first_coord, second_coord)) {
        RTUTIL.swapByCMP(ll_rect, ur_rect,
                         [](PlanarRect& a, PlanarRect& b) { return CmpPlanarCoordByXASC()(a.getMidPoint(), b.getMidPoint()); });
        std::vector<int32_t> ll_scale_list = RTUTIL.getScaleList(ll_rect.get_ll_x(), ll_rect.get_ur_x(), x_track_grid_list);
        std::vector<int32_t> ur_scale_list = RTUTIL.getScaleList(ur_rect.get_ll_x(), ur_rect.get_ur_x(), x_track_grid_list);
        auto ll_iter = ll_scale_list.rbegin();
        auto ur_iter = ur_scale_list.begin();
        while (ll_iter != ll_scale_list.rend() && ur_iter != ur_scale_list.end()) {
          if (*ll_iter != *ur_iter) {
            break;
          }
          ++ll_iter;
          ++ur_iter;
        }
        int32_t ll_x = *ll_iter;
        int32_t ur_x = *ur_iter;
        for (int32_t y : RTUTIL.getScaleList(ll_rect.get_ll_y(), ll_rect.get_ur_y(), y_track_grid_list)) {
          ta_group_list.front().get_coord_list().emplace_back(ll_x, y, layer_idx);
          ta_group_list.back().get_coord_list().emplace_back(ur_x, y, layer_idx);
        }
      } else if (RTUTIL.isVertical(first_coord, second_coord)) {
        RTUTIL.swapByCMP(ll_rect, ur_rect,
                         [](PlanarRect& a, PlanarRect& b) { return CmpPlanarCoordByYASC()(a.getMidPoint(), b.getMidPoint()); });
        std::vector<int32_t> ll_scale_list = RTUTIL.getScaleList(ll_rect.get_ll_y(), ll_rect.get_ur_y(), y_track_grid_list);
        std::vector<int32_t> ur_scale_list = RTUTIL.getScaleList(ur_rect.get_ll_y(), ur_rect.get_ur_y(), y_track_grid_list);
        auto ll_iter = ll_scale_list.rbegin();
        auto ur_iter = ur_scale_list.begin();
        while (ll_iter != ll_scale_list.rend() && ur_iter != ur_scale_list.end()) {
          if (*ll_iter != *ur_iter) {
            break;
          }
          ++ll_iter;
          ++ur_iter;
        }
        int32_t ll_y = *ll_iter;
        int32_t ur_y = *ur_iter;
        for (int32_t x : RTUTIL.getScaleList(ll_rect.get_ll_x(), ll_rect.get_ur_x(), x_track_grid_list)) {
          ta_group_list.front().get_coord_list().emplace_back(x, ll_y, layer_idx);
          ta_group_list.back().get_coord_list().emplace_back(x, ur_y, layer_idx);
        }
      }
      TATask* ta_task = new TATask();
      ta_task->set_net_idx(ta_net.get_net_idx());
      ta_task->set_task_idx(static_cast<int32_t>(ta_task_list.size()));
      ta_task->set_connect_type(ta_net.get_connect_type());
      ta_task->set_ta_group_list(ta_group_list);
      {
        std::vector<PlanarCoord> coord_list;
        for (TAGroup& ta_group : ta_task->get_ta_group_list()) {
          for (LayerCoord& coord : ta_group.get_coord_list()) {
            coord_list.push_back(coord);
          }
        }
        ta_task->set_bounding_box(RTUTIL.getBoundingBox(coord_list));
      }
      ta_task->set_routed_times(0);
      ta_task_list.push_back(ta_task);
    }
  }
  std::sort(ta_task_list.begin(), ta_task_list.end(), CmpTATask());
}

bool TrackAssigner::needRouting(TAPanel& ta_panel)
{
  if (ta_panel.get_ta_task_list().empty()) {
    return false;
  }
  return true;
}

void TrackAssigner::buildPanelTrackAxis(TAPanel& ta_panel)
{
  std::vector<int32_t> x_scale_list;
  std::vector<int32_t> y_scale_list;

  for (TATask* ta_task : ta_panel.get_ta_task_list()) {
    for (TAGroup& ta_group : ta_task->get_ta_group_list()) {
      for (LayerCoord& coord : ta_group.get_coord_list()) {
        x_scale_list.push_back(coord.get_x());
        y_scale_list.push_back(coord.get_y());
      }
    }
  }

  ScaleAxis& panel_track_axis = ta_panel.get_panel_track_axis();
  std::sort(x_scale_list.begin(), x_scale_list.end());
  x_scale_list.erase(std::unique(x_scale_list.begin(), x_scale_list.end()), x_scale_list.end());
  panel_track_axis.set_x_grid_list(RTUTIL.makeScaleGridList(x_scale_list));
  std::sort(y_scale_list.begin(), y_scale_list.end());
  y_scale_list.erase(std::unique(y_scale_list.begin(), y_scale_list.end()), y_scale_list.end());
  panel_track_axis.set_y_grid_list(RTUTIL.makeScaleGridList(y_scale_list));
}

void TrackAssigner::buildTANodeMap(TAPanel& ta_panel)
{
  PlanarCoord& real_ll = ta_panel.get_panel_rect().get_real_ll();
  PlanarCoord& real_ur = ta_panel.get_panel_rect().get_real_ur();
  ScaleAxis& panel_track_axis = ta_panel.get_panel_track_axis();
  std::vector<int32_t> x_list = RTUTIL.getScaleList(real_ll.get_x(), real_ur.get_x(), panel_track_axis.get_x_grid_list());
  std::vector<int32_t> y_list = RTUTIL.getScaleList(real_ll.get_y(), real_ur.get_y(), panel_track_axis.get_y_grid_list());

  GridMap<TANode>& ta_node_map = ta_panel.get_ta_node_map();
  ta_node_map.init(x_list.size(), y_list.size());
  for (size_t x = 0; x < x_list.size(); x++) {
    for (size_t y = 0; y < y_list.size(); y++) {
      TANode& ta_node = ta_node_map[x][y];
      ta_node.set_x(x_list[x]);
      ta_node.set_y(y_list[y]);
      ta_node.set_layer_idx(ta_panel.get_panel_rect().get_layer_idx());
    }
  }
}

void TrackAssigner::buildTANodeNeighbor(TAPanel& ta_panel)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  GridMap<TANode>& ta_node_map = ta_panel.get_ta_node_map();
  for (int32_t x = 0; x < ta_node_map.get_x_size(); x++) {
    for (int32_t y = 0; y < ta_node_map.get_y_size(); y++) {
      std::map<Orientation, TANode*>& neighbor_node_map = ta_node_map[x][y].get_neighbor_node_map();
      if (routing_layer_list[ta_panel.get_panel_rect().get_layer_idx()].isPreferH()) {
        if (x != 0) {
          neighbor_node_map[Orientation::kWest] = &ta_node_map[x - 1][y];
        }
        if (x != (ta_node_map.get_x_size() - 1)) {
          neighbor_node_map[Orientation::kEast] = &ta_node_map[x + 1][y];
        }
      } else {
        if (y != 0) {
          neighbor_node_map[Orientation::kSouth] = &ta_node_map[x][y - 1];
        }
        if (y != (ta_node_map.get_y_size() - 1)) {
          neighbor_node_map[Orientation::kNorth] = &ta_node_map[x][y + 1];
        }
      }
    }
  }
}

void TrackAssigner::buildOrientNetMap(TAPanel& ta_panel)
{
  for (auto& [is_routing, layer_net_fixed_rect_map] : ta_panel.get_type_layer_net_fixed_rect_map()) {
    for (auto& [layer_idx, net_fixed_rect_map] : layer_net_fixed_rect_map) {
      if (is_routing != true || layer_idx != ta_panel.get_panel_rect().get_layer_idx()) {
        continue;
      }
      for (auto& [net_idx, fixed_rect_set] : net_fixed_rect_map) {
        for (auto& fixed_rect : fixed_rect_set) {
          updateFixedRectToGraph(ta_panel, ChangeType::kAdd, net_idx, fixed_rect, true);
        }
      }
      break;
    }
  }
  for (auto& [net_idx, segment_set] : ta_panel.get_net_access_result_map()) {
    for (Segment<LayerCoord>* segment : segment_set) {
      updateFixedRectToGraph(ta_panel, ChangeType::kAdd, net_idx, *segment);
    }
  }
}

void TrackAssigner::routeTAPanel(TAPanel& ta_panel)
{
  int32_t enable_lsa = RTDM.getConfig().enable_lsa;

  if (!enable_lsa) {
    routeTAPanelBySelf(ta_panel);
  } else {
    routeTAPanelByOther(ta_panel);
  }
}

void TrackAssigner::routeTAPanelBySelf(TAPanel& ta_panel)
{
  std::vector<TATask*> ta_task_list = initTaskSchedule(ta_panel);
  while (!ta_task_list.empty()) {
    for (TATask* ta_task : ta_task_list) {
      routeTATask(ta_panel, ta_task);
      ta_task->addRoutedTimes();
    }
    updateViolationList(ta_panel);
    ta_task_list = getTaskScheduleByViolation(ta_panel);
  }
}

std::vector<TATask*> TrackAssigner::initTaskSchedule(TAPanel& ta_panel)
{
  std::vector<TATask*> ta_task_list;
  for (TATask* ta_task : ta_panel.get_ta_task_list()) {
    ta_task_list.push_back(ta_task);
  }
  return ta_task_list;
}

void TrackAssigner::routeTATask(TAPanel& ta_panel, TATask* ta_task)
{
  initSingleTask(ta_panel, ta_task);
  while (!isConnectedAllEnd(ta_panel)) {
    routeSinglePath(ta_panel);
    updatePathResult(ta_panel);
    updateDirectionSet(ta_panel);
    resetStartAndEnd(ta_panel);
    resetSinglePath(ta_panel);
  }
  updateTaskResult(ta_panel);
  resetSingleTask(ta_panel);
}

void TrackAssigner::initSingleTask(TAPanel& ta_panel, TATask* ta_task)
{
  ScaleAxis& panel_track_axis = ta_panel.get_panel_track_axis();
  GridMap<TANode>& ta_node_map = ta_panel.get_ta_node_map();

  // single task
  ta_panel.set_curr_ta_task(ta_task);
  {
    std::vector<std::vector<TANode*>> node_list_list;
    std::vector<TAGroup>& ta_group_list = ta_task->get_ta_group_list();
    for (TAGroup& ta_group : ta_group_list) {
      std::vector<TANode*> node_list;
      for (LayerCoord& coord : ta_group.get_coord_list()) {
        if (!RTUTIL.existTrackGrid(coord, panel_track_axis)) {
          RTLOG.error(Loc::current(), "The coord can not find grid!");
        }
        PlanarCoord grid_coord = RTUTIL.getTrackGrid(coord, panel_track_axis);
        TANode& ta_node = ta_node_map[grid_coord.get_x()][grid_coord.get_y()];
        node_list.push_back(&ta_node);
      }
      node_list_list.push_back(node_list);
    }
    for (size_t i = 0; i < node_list_list.size(); i++) {
      if (i == 0) {
        ta_panel.get_start_node_list_list().push_back(node_list_list[i]);
      } else {
        ta_panel.get_end_node_list_list().push_back(node_list_list[i]);
      }
    }
  }
  ta_panel.get_path_node_list().clear();
  ta_panel.get_single_task_visited_node_list().clear();
  ta_panel.get_routing_segment_list().clear();
}

bool TrackAssigner::isConnectedAllEnd(TAPanel& ta_panel)
{
  return ta_panel.get_end_node_list_list().empty();
}

void TrackAssigner::routeSinglePath(TAPanel& ta_panel)
{
  initPathHead(ta_panel);
  while (!searchEnded(ta_panel)) {
    expandSearching(ta_panel);
    resetPathHead(ta_panel);
  }
}

void TrackAssigner::initPathHead(TAPanel& ta_panel)
{
  std::vector<std::vector<TANode*>>& start_node_list_list = ta_panel.get_start_node_list_list();
  std::vector<TANode*>& path_node_list = ta_panel.get_path_node_list();

  for (std::vector<TANode*>& start_node_list : start_node_list_list) {
    for (TANode* start_node : start_node_list) {
      start_node->set_estimated_cost(getEstimateCostToEnd(ta_panel, start_node));
      pushToOpenList(ta_panel, start_node);
    }
  }
  for (TANode* path_node : path_node_list) {
    path_node->set_estimated_cost(getEstimateCostToEnd(ta_panel, path_node));
    pushToOpenList(ta_panel, path_node);
  }
  resetPathHead(ta_panel);
}

bool TrackAssigner::searchEnded(TAPanel& ta_panel)
{
  std::vector<std::vector<TANode*>>& end_node_list_list = ta_panel.get_end_node_list_list();
  TANode* path_head_node = ta_panel.get_path_head_node();

  if (path_head_node == nullptr) {
    ta_panel.set_end_node_list_idx(-1);
    return true;
  }
  for (size_t i = 0; i < end_node_list_list.size(); i++) {
    for (TANode* end_node : end_node_list_list[i]) {
      if (path_head_node == end_node) {
        ta_panel.set_end_node_list_idx(static_cast<int32_t>(i));
        return true;
      }
    }
  }
  return false;
}

void TrackAssigner::expandSearching(TAPanel& ta_panel)
{
  PriorityQueue<TANode*, std::vector<TANode*>, CmpTANodeCost>& open_queue = ta_panel.get_open_queue();
  TANode* path_head_node = ta_panel.get_path_head_node();

  for (auto& [orientation, neighbor_node] : path_head_node->get_neighbor_node_map()) {
    if (neighbor_node == nullptr) {
      continue;
    }
    if (!RTUTIL.isInside(ta_panel.get_curr_ta_task()->get_bounding_box(), *neighbor_node)) {
      continue;
    }
    if (neighbor_node->isClose()) {
      continue;
    }
    double know_cost = getKnowCost(ta_panel, path_head_node, neighbor_node);
    if (neighbor_node->isOpen() && know_cost < neighbor_node->get_known_cost()) {
      neighbor_node->set_known_cost(know_cost);
      neighbor_node->set_parent_node(path_head_node);
      // 对优先队列中的值修改了，需要重新建堆
      std::make_heap(open_queue.begin(), open_queue.end(), CmpTANodeCost());
    } else if (neighbor_node->isNone()) {
      neighbor_node->set_known_cost(know_cost);
      neighbor_node->set_parent_node(path_head_node);
      neighbor_node->set_estimated_cost(getEstimateCostToEnd(ta_panel, neighbor_node));
      pushToOpenList(ta_panel, neighbor_node);
    }
  }
}

void TrackAssigner::resetPathHead(TAPanel& ta_panel)
{
  ta_panel.set_path_head_node(popFromOpenList(ta_panel));
}

bool TrackAssigner::isRoutingFailed(TAPanel& ta_panel)
{
  return ta_panel.get_end_node_list_idx() == -1;
}

void TrackAssigner::resetSinglePath(TAPanel& ta_panel)
{
  PriorityQueue<TANode*, std::vector<TANode*>, CmpTANodeCost> empty_queue;
  ta_panel.set_open_queue(empty_queue);

  std::vector<TANode*>& single_path_visited_node_list = ta_panel.get_single_path_visited_node_list();
  for (TANode* visited_node : single_path_visited_node_list) {
    visited_node->set_state(TANodeState::kNone);
    visited_node->set_parent_node(nullptr);
    visited_node->set_known_cost(0);
    visited_node->set_estimated_cost(0);
  }
  single_path_visited_node_list.clear();

  ta_panel.set_path_head_node(nullptr);
  ta_panel.set_end_node_list_idx(-1);
}

void TrackAssigner::updatePathResult(TAPanel& ta_panel)
{
  for (Segment<LayerCoord>& routing_segment : getRoutingSegmentListByNode(ta_panel.get_path_head_node())) {
    ta_panel.get_routing_segment_list().push_back(routing_segment);
  }
}

std::vector<Segment<LayerCoord>> TrackAssigner::getRoutingSegmentListByNode(TANode* node)
{
  std::vector<Segment<LayerCoord>> routing_segment_list;

  TANode* curr_node = node;
  TANode* pre_node = curr_node->get_parent_node();

  if (pre_node == nullptr) {
    // 起点和终点重合
    return routing_segment_list;
  }
  Orientation curr_orientation = RTUTIL.getOrientation(*curr_node, *pre_node);
  while (pre_node->get_parent_node() != nullptr) {
    Orientation pre_orientation = RTUTIL.getOrientation(*pre_node, *pre_node->get_parent_node());
    if (curr_orientation != pre_orientation) {
      routing_segment_list.emplace_back(*curr_node, *pre_node);
      curr_orientation = pre_orientation;
      curr_node = pre_node;
    }
    pre_node = pre_node->get_parent_node();
  }
  routing_segment_list.emplace_back(*curr_node, *pre_node);

  return routing_segment_list;
}

void TrackAssigner::updateDirectionSet(TAPanel& ta_panel)
{
  TANode* path_head_node = ta_panel.get_path_head_node();

  TANode* curr_node = path_head_node;
  TANode* pre_node = curr_node->get_parent_node();
  while (pre_node != nullptr) {
    curr_node->get_direction_set().insert(RTUTIL.getDirection(*curr_node, *pre_node));
    pre_node->get_direction_set().insert(RTUTIL.getDirection(*pre_node, *curr_node));
    curr_node = pre_node;
    pre_node = curr_node->get_parent_node();
  }
}

void TrackAssigner::resetStartAndEnd(TAPanel& ta_panel)
{
  std::vector<std::vector<TANode*>>& start_node_list_list = ta_panel.get_start_node_list_list();
  std::vector<std::vector<TANode*>>& end_node_list_list = ta_panel.get_end_node_list_list();
  std::vector<TANode*>& path_node_list = ta_panel.get_path_node_list();
  TANode* path_head_node = ta_panel.get_path_head_node();
  int32_t end_node_list_idx = ta_panel.get_end_node_list_idx();

  // 对于抵达的终点pin，只保留到达的node
  end_node_list_list[end_node_list_idx].clear();
  end_node_list_list[end_node_list_idx].push_back(path_head_node);

  TANode* path_node = path_head_node->get_parent_node();
  if (path_node == nullptr) {
    // 起点和终点重合
    path_node = path_head_node;
  } else {
    // 起点和终点不重合
    while (path_node->get_parent_node() != nullptr) {
      path_node_list.push_back(path_node);
      path_node = path_node->get_parent_node();
    }
  }
  if (start_node_list_list.size() == 1) {
    // 初始化时，要把start_node_list_list的pin只留一个ap点
    // 后续只要将end_node_list_list的pin保留一个ap点
    start_node_list_list.front().clear();
    start_node_list_list.front().push_back(path_node);
  }
  start_node_list_list.push_back(end_node_list_list[end_node_list_idx]);
  end_node_list_list.erase(end_node_list_list.begin() + end_node_list_idx);
}

void TrackAssigner::updateTaskResult(TAPanel& ta_panel)
{
  std::vector<Segment<LayerCoord>> new_routing_segment_list = getRoutingSegmentList(ta_panel);

  int32_t curr_net_idx = ta_panel.get_curr_ta_task()->get_net_idx();
  int32_t curr_task_idx = ta_panel.get_curr_ta_task()->get_task_idx();
  std::vector<Segment<LayerCoord>>& routing_segment_list = ta_panel.get_net_task_result_map()[curr_net_idx][curr_task_idx];
  // 原结果从graph删除
  for (Segment<LayerCoord>& routing_segment : routing_segment_list) {
    updateNetResultToGraph(ta_panel, ChangeType::kDel, curr_net_idx, routing_segment);
  }
  routing_segment_list = new_routing_segment_list;
  // 新结果添加到graph
  for (Segment<LayerCoord>& routing_segment : routing_segment_list) {
    updateNetResultToGraph(ta_panel, ChangeType::kAdd, curr_net_idx, routing_segment);
  }
}

std::vector<Segment<LayerCoord>> TrackAssigner::getRoutingSegmentList(TAPanel& ta_panel)
{
  TATask* curr_ta_task = ta_panel.get_curr_ta_task();

  std::vector<LayerCoord> candidate_root_coord_list;
  std::map<LayerCoord, std::set<int32_t>, CmpLayerCoordByXASC> key_coord_pin_map;
  std::vector<TAGroup>& ta_group_list = curr_ta_task->get_ta_group_list();
  for (size_t i = 0; i < ta_group_list.size(); i++) {
    for (LayerCoord& coord : ta_group_list[i].get_coord_list()) {
      candidate_root_coord_list.push_back(coord);
      key_coord_pin_map[coord].insert(static_cast<int32_t>(i));
    }
  }
  MTree<LayerCoord> coord_tree
      = RTUTIL.getTreeByFullFlow(candidate_root_coord_list, ta_panel.get_routing_segment_list(), key_coord_pin_map);

  std::vector<Segment<LayerCoord>> routing_segment_list;
  for (Segment<TNode<LayerCoord>*>& coord_segment : RTUTIL.getSegListByTree(coord_tree)) {
    routing_segment_list.emplace_back(coord_segment.get_first()->value(), coord_segment.get_second()->value());
  }
  return routing_segment_list;
}

void TrackAssigner::resetSingleTask(TAPanel& ta_panel)
{
  ta_panel.set_curr_ta_task(nullptr);
  ta_panel.get_start_node_list_list().clear();
  ta_panel.get_end_node_list_list().clear();
  ta_panel.get_path_node_list().clear();

  std::vector<TANode*>& single_task_visited_node_list = ta_panel.get_single_task_visited_node_list();
  for (TANode* single_task_visited_node : single_task_visited_node_list) {
    single_task_visited_node->get_direction_set().clear();
  }
  single_task_visited_node_list.clear();

  ta_panel.get_routing_segment_list().clear();
}

// manager open list

void TrackAssigner::pushToOpenList(TAPanel& ta_panel, TANode* curr_node)
{
  PriorityQueue<TANode*, std::vector<TANode*>, CmpTANodeCost>& open_queue = ta_panel.get_open_queue();
  std::vector<TANode*>& single_task_visited_node_list = ta_panel.get_single_task_visited_node_list();
  std::vector<TANode*>& single_path_visited_node_list = ta_panel.get_single_path_visited_node_list();

  open_queue.push(curr_node);
  curr_node->set_state(TANodeState::kOpen);
  single_task_visited_node_list.push_back(curr_node);
  single_path_visited_node_list.push_back(curr_node);
}

TANode* TrackAssigner::popFromOpenList(TAPanel& ta_panel)
{
  PriorityQueue<TANode*, std::vector<TANode*>, CmpTANodeCost>& open_queue = ta_panel.get_open_queue();

  TANode* node = nullptr;
  if (!open_queue.empty()) {
    node = open_queue.top();
    open_queue.pop();
    node->set_state(TANodeState::kClose);
  }
  return node;
}

// calculate known cost

double TrackAssigner::getKnowCost(TAPanel& ta_panel, TANode* start_node, TANode* end_node)
{
  bool exist_neighbor = false;
  for (auto& [orientation, neighbor_ptr] : start_node->get_neighbor_node_map()) {
    if (neighbor_ptr == end_node) {
      exist_neighbor = true;
      break;
    }
  }
  if (!exist_neighbor) {
    RTLOG.error(Loc::current(), "The neighbor not exist!");
  }

  double cost = 0;
  cost += start_node->get_known_cost();
  cost += getNodeCost(ta_panel, start_node, RTUTIL.getOrientation(*start_node, *end_node));
  cost += getNodeCost(ta_panel, end_node, RTUTIL.getOrientation(*end_node, *start_node));
  cost += getKnowWireCost(ta_panel, start_node, end_node);
  cost += getKnowCornerCost(ta_panel, start_node, end_node);
  cost += getKnowViaCost(ta_panel, start_node, end_node);
  return cost;
}

double TrackAssigner::getNodeCost(TAPanel& ta_panel, TANode* curr_node, Orientation orientation)
{
  double fixed_rect_unit = ta_panel.get_ta_parameter()->get_fixed_rect_unit();
  double routed_rect_unit = ta_panel.get_ta_parameter()->get_routed_rect_unit();
  double violation_unit = ta_panel.get_ta_parameter()->get_violation_unit();

  int32_t net_idx = ta_panel.get_curr_ta_task()->get_net_idx();

  double cost = 0;
  cost += curr_node->getFixedRectCost(net_idx, orientation, fixed_rect_unit);
  cost += curr_node->getRoutedRectCost(net_idx, orientation, routed_rect_unit);
  cost += curr_node->getViolationCost(orientation, violation_unit);
  return cost;
}

double TrackAssigner::getKnowWireCost(TAPanel& ta_panel, TANode* start_node, TANode* end_node)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  double prefer_wire_unit = ta_panel.get_ta_parameter()->get_prefer_wire_unit();

  double wire_cost = 0;
  if (start_node->get_layer_idx() == end_node->get_layer_idx()) {
    wire_cost += RTUTIL.getManhattanDistance(start_node->get_planar_coord(), end_node->get_planar_coord());

    RoutingLayer& routing_layer = routing_layer_list[start_node->get_layer_idx()];
    if (routing_layer.get_prefer_direction() == RTUTIL.getDirection(*start_node, *end_node)) {
      wire_cost *= prefer_wire_unit;
    }
  }
  return wire_cost;
}

double TrackAssigner::getKnowCornerCost(TAPanel& ta_panel, TANode* start_node, TANode* end_node)
{
  double corner_unit = ta_panel.get_ta_parameter()->get_corner_unit();

  double corner_cost = 0;
  if (start_node->get_layer_idx() == end_node->get_layer_idx()) {
    std::set<Direction> direction_set;
    // 添加start direction
    std::set<Direction>& start_direction_set = start_node->get_direction_set();
    direction_set.insert(start_direction_set.begin(), start_direction_set.end());
    // 添加start到parent的direction
    if (start_node->get_parent_node() != nullptr) {
      direction_set.insert(RTUTIL.getDirection(*start_node->get_parent_node(), *start_node));
    }
    // 添加end direction
    std::set<Direction>& end_direction_set = end_node->get_direction_set();
    direction_set.insert(end_direction_set.begin(), end_direction_set.end());
    // 添加start到end的direction
    direction_set.insert(RTUTIL.getDirection(*start_node, *end_node));

    if (direction_set.size() == 2) {
      corner_cost += corner_unit;
    } else if (direction_set.size() == 2) {
      RTLOG.error(Loc::current(), "Direction set is error!");
    }
  }
  return corner_cost;
}

double TrackAssigner::getKnowViaCost(TAPanel& ta_panel, TANode* start_node, TANode* end_node)
{
  return 0;
}

// calculate estimate cost

double TrackAssigner::getEstimateCostToEnd(TAPanel& ta_panel, TANode* curr_node)
{
  std::vector<std::vector<TANode*>>& end_node_list_list = ta_panel.get_end_node_list_list();

  double estimate_cost = DBL_MAX;
  for (std::vector<TANode*>& end_node_list : end_node_list_list) {
    for (TANode* end_node : end_node_list) {
      if (end_node->isClose()) {
        continue;
      }
      estimate_cost = std::min(estimate_cost, getEstimateCost(ta_panel, curr_node, end_node));
    }
  }
  return estimate_cost;
}

double TrackAssigner::getEstimateCost(TAPanel& ta_panel, TANode* start_node, TANode* end_node)
{
  double estimate_cost = 0;
  estimate_cost += getEstimateWireCost(ta_panel, start_node, end_node);
  estimate_cost += getEstimateCornerCost(ta_panel, start_node, end_node);
  estimate_cost += getEstimateViaCost(ta_panel, start_node, end_node);
  return estimate_cost;
}

double TrackAssigner::getEstimateWireCost(TAPanel& ta_panel, TANode* start_node, TANode* end_node)
{
  double prefer_wire_unit = ta_panel.get_ta_parameter()->get_prefer_wire_unit();

  double wire_cost = 0;
  wire_cost += RTUTIL.getManhattanDistance(start_node->get_planar_coord(), end_node->get_planar_coord());
  wire_cost *= prefer_wire_unit;
  return wire_cost;
}

double TrackAssigner::getEstimateCornerCost(TAPanel& ta_panel, TANode* start_node, TANode* end_node)
{
  double corner_unit = ta_panel.get_ta_parameter()->get_corner_unit();

  double corner_cost = 0;
  if (start_node->get_layer_idx() == end_node->get_layer_idx()) {
    if (RTUTIL.isOblique(*start_node, *end_node)) {
      corner_cost += corner_unit;
    }
  }
  return corner_cost;
}

double TrackAssigner::getEstimateViaCost(TAPanel& ta_panel, TANode* start_node, TANode* end_node)
{
  return 0;
}

void TrackAssigner::updateViolationList(TAPanel& ta_panel)
{
  std::vector<Violation> new_violation_list = getViolationList(ta_panel);

  std::vector<Violation>& violation_list = ta_panel.get_violation_list();
  // 原结果从graph删除
  for (Violation& violation : violation_list) {
    updateViolationToGraph(ta_panel, ChangeType::kDel, violation);
  }
  violation_list = new_violation_list;
  // 新结果添加到graph
  for (Violation& violation : violation_list) {
    updateViolationToGraph(ta_panel, ChangeType::kAdd, violation);
  }
}

std::vector<Violation> TrackAssigner::getViolationList(TAPanel& ta_panel)
{
  std::string top_name
      = RTUTIL.getString("ta_panel_", ta_panel.get_ta_panel_id().get_layer_idx(), "_", ta_panel.get_ta_panel_id().get_panel_idx());
  std::vector<std::pair<EXTLayerRect*, bool>> env_shape_list;
  std::map<int32_t, std::vector<std::pair<EXTLayerRect*, bool>>> net_pin_shape_map;
  for (auto& [is_routing, layer_net_fixed_rect_map] : ta_panel.get_type_layer_net_fixed_rect_map()) {
    for (auto& [layer_idx, net_fixed_rect_map] : layer_net_fixed_rect_map) {
      if (is_routing != true || layer_idx != ta_panel.get_panel_rect().get_layer_idx()) {
        continue;
      }
      for (auto& [net_idx, fixed_rect_set] : net_fixed_rect_map) {
        if (net_idx == -1) {
          for (auto& fixed_rect : fixed_rect_set) {
            env_shape_list.emplace_back(fixed_rect, true);
          }
        } else {
          for (auto& fixed_rect : fixed_rect_set) {
            net_pin_shape_map[net_idx].emplace_back(fixed_rect, true);
          }
        }
      }
      break;
    }
  }
  std::map<int32_t, std::vector<Segment<LayerCoord>>> net_fixed_result_map;
  for (auto& [net_idx, segment_set] : ta_panel.get_net_access_result_map()) {
    for (Segment<LayerCoord>* segment : segment_set) {
      net_fixed_result_map[net_idx].push_back(*segment);
    }
  }
  std::map<int32_t, std::vector<Segment<LayerCoord>>> net_routing_result_map;
  for (auto& [net_idx, task_result_map] : ta_panel.get_net_task_result_map()) {
    for (auto& [task_idx, segment_list] : task_result_map) {
      for (Segment<LayerCoord>& segment : segment_list) {
        net_routing_result_map[net_idx].emplace_back(segment);
      }
    }
  }
  std::string stage = "TA";
  return RTDE.getViolationList(top_name, env_shape_list, net_pin_shape_map, net_fixed_result_map, net_routing_result_map, stage);
}

std::vector<TATask*> TrackAssigner::getTaskScheduleByViolation(TAPanel& ta_panel)
{
  int32_t max_routed_times = ta_panel.get_ta_parameter()->get_max_routed_times();

  std::set<int32_t> violation_net_set;
  for (Violation& violation : ta_panel.get_violation_list()) {
    for (int32_t violation_net : violation.get_violation_net_set()) {
      violation_net_set.insert(violation_net);
    }
  }
  std::vector<TATask*> ta_task_list;
  for (TATask* ta_task : ta_panel.get_ta_task_list()) {
    if (!RTUTIL.exist(violation_net_set, ta_task->get_net_idx())) {
      continue;
    }
    if (ta_task->get_routed_times() >= max_routed_times) {
      continue;
    }
    ta_task_list.push_back(ta_task);
  }
  return ta_task_list;
}

void TrackAssigner::routeTAPanelByOther(TAPanel& ta_panel)
{
  RTI.routeTAPanel(ta_panel);
  updateViolationList(ta_panel);
}

void TrackAssigner::uploadNetResult(TAPanel& ta_panel)
{
  for (auto& [net_idx, task_result_map] : ta_panel.get_net_task_result_map()) {
    for (auto& [task_idx, segment_list] : task_result_map) {
      for (Segment<LayerCoord>& segment : segment_list) {
        RTDM.updateDetailedNetResultToGCellMap(ChangeType::kAdd, net_idx, new Segment<LayerCoord>(segment));
      }
    }
  }
}

void TrackAssigner::uploadViolation(TAPanel& ta_panel)
{
  for (Violation& violation : ta_panel.get_violation_list()) {
    RTDM.updateViolationToGCellMap(ChangeType::kAdd, new Violation(violation));
  }
}

void TrackAssigner::freeTAPanel(TAPanel& ta_panel)
{
  for (TATask* ta_task : ta_panel.get_ta_task_list()) {
    delete ta_task;
    ta_task = nullptr;
  }
  ta_panel.get_ta_task_list().clear();
  ta_panel.get_ta_node_map().free();
}

int32_t TrackAssigner::getViolationNum()
{
  Die& die = RTDM.getDatabase().get_die();

  return static_cast<int32_t>(RTDM.getViolationSet(die).size());
}

#if 1  // update env

void TrackAssigner::updateFixedRectToGraph(TAPanel& ta_panel, ChangeType change_type, int32_t net_idx, EXTLayerRect* fixed_rect,
                                           bool is_routing)
{
  NetShape net_shape(net_idx, fixed_rect->getRealLayerRect(), is_routing);
  if (!net_shape.get_is_routing() || (ta_panel.get_ta_panel_id().get_layer_idx() != net_shape.get_layer_idx())) {
    return;
  }
  for (auto& [ta_node, orientation_set] : getNodeOrientationMap(ta_panel, net_shape)) {
    for (Orientation orientation : orientation_set) {
      if (change_type == ChangeType::kAdd) {
        ta_node->get_orient_fixed_rect_map()[orientation].insert(net_shape.get_net_idx());
      } else if (change_type == ChangeType::kDel) {
        ta_node->get_orient_fixed_rect_map()[orientation].erase(net_shape.get_net_idx());
      }
    }
  }
}

void TrackAssigner::updateFixedRectToGraph(TAPanel& ta_panel, ChangeType change_type, int32_t net_idx, Segment<LayerCoord>& segment)
{
  for (NetShape& net_shape : RTDM.getNetShapeList(net_idx, segment)) {
    if (!net_shape.get_is_routing() || (ta_panel.get_ta_panel_id().get_layer_idx() != net_shape.get_layer_idx())) {
      continue;
    }
    for (auto& [ta_node, orientation_set] : getNodeOrientationMap(ta_panel, net_shape)) {
      for (Orientation orientation : orientation_set) {
        if (change_type == ChangeType::kAdd) {
          ta_node->get_orient_fixed_rect_map()[orientation].insert(net_shape.get_net_idx());
        } else if (change_type == ChangeType::kDel) {
          ta_node->get_orient_fixed_rect_map()[orientation].erase(net_shape.get_net_idx());
        }
      }
    }
  }
}

void TrackAssigner::updateNetResultToGraph(TAPanel& ta_panel, ChangeType change_type, int32_t net_idx, Segment<LayerCoord>& segment)
{
  for (NetShape& net_shape : RTDM.getNetShapeList(net_idx, segment)) {
    if (!net_shape.get_is_routing() || (ta_panel.get_ta_panel_id().get_layer_idx() != net_shape.get_layer_idx())) {
      continue;
    }
    for (auto& [ta_node, orientation_set] : getNodeOrientationMap(ta_panel, net_shape)) {
      for (Orientation orientation : orientation_set) {
        if (change_type == ChangeType::kAdd) {
          ta_node->get_orient_routed_rect_map()[orientation].insert(net_shape.get_net_idx());
        } else if (change_type == ChangeType::kDel) {
          ta_node->get_orient_routed_rect_map()[orientation].erase(net_shape.get_net_idx());
        }
      }
    }
  }
}

void TrackAssigner::updateViolationToGraph(TAPanel& ta_panel, ChangeType change_type, Violation& violation)
{
  NetShape net_shape(-1, violation.get_violation_shape().getRealLayerRect(), violation.get_is_routing());
  if (!net_shape.get_is_routing() || (ta_panel.get_ta_panel_id().get_layer_idx() != net_shape.get_layer_idx())) {
    return;
  }
  for (auto& [ta_node, orientation_set] : getNodeOrientationMap(ta_panel, net_shape)) {
    for (Orientation orientation : orientation_set) {
      if (change_type == ChangeType::kAdd) {
        ta_node->get_orient_violation_number_map()[orientation]++;
      } else if (change_type == ChangeType::kDel) {
        ta_node->get_orient_violation_number_map()[orientation]--;
      }
    }
  }
}

std::map<TANode*, std::set<Orientation>> TrackAssigner::getNodeOrientationMap(TAPanel& ta_panel, NetShape& net_shape)
{
  std::map<TANode*, std::set<Orientation>> node_orientation_map;
  if (net_shape.get_is_routing()) {
    node_orientation_map = getRoutingNodeOrientationMap(ta_panel, net_shape);
  } else {
    RTLOG.error(Loc::current(), "The type of net_shape is cut!");
  }
  return node_orientation_map;
}

std::map<TANode*, std::set<Orientation>> TrackAssigner::getRoutingNodeOrientationMap(TAPanel& ta_panel, NetShape& net_shape)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  if (!net_shape.get_is_routing()) {
    RTLOG.error(Loc::current(), "The type of net_shape is cut!");
  }
  if (ta_panel.get_panel_rect().get_layer_idx() != net_shape.get_layer_idx()) {
    RTLOG.error(Loc::current(), "The layer_idx is error!");
  }
  int32_t layer_idx = net_shape.get_layer_idx();
  RoutingLayer& routing_layer = routing_layer_list[layer_idx];
  int32_t min_spacing = routing_layer.getMinSpacing(net_shape.get_rect());
  int32_t half_wire_width = routing_layer.get_min_width() / 2;

  GridMap<TANode>& ta_node_map = ta_panel.get_ta_node_map();
  std::map<TANode*, std::set<Orientation>> node_orientation_map;
  // wire
  {
    // 膨胀size为 min_spacing + half_wire_width
    int32_t enlarged_size = min_spacing + half_wire_width;
    // 贴合的也不算违例
    enlarged_size -= 1;
    PlanarRect planar_enlarged_rect = RTUTIL.getEnlargedRect(net_shape.get_rect(), enlarged_size);
    for (auto& [grid_coord, orientation_set] : RTUTIL.getTrackGridOrientationMap(planar_enlarged_rect, ta_panel.get_panel_track_axis())) {
      TANode& node = ta_node_map[grid_coord.get_x()][grid_coord.get_y()];
      for (const Orientation& orientation : orientation_set) {
        if (orientation == Orientation::kAbove || orientation == Orientation::kBelow) {
          continue;
        }
        if (!RTUTIL.exist(node.get_neighbor_node_map(), orientation)) {
          continue;
        }
        node_orientation_map[&node].insert(orientation);
        node_orientation_map[node.get_neighbor_node_map()[orientation]].insert(RTUTIL.getOppositeOrientation(orientation));
      }
    }
  }
  return node_orientation_map;
}

#endif

#if 1  // exhibit

void TrackAssigner::updateSummary(TAModel& ta_model)
{
  int32_t micron_dbu = RTDM.getDatabase().get_micron_dbu();
  Die& die = RTDM.getDatabase().get_die();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::map<int32_t, double>& routing_wire_length_map = RTDM.getSummary().ta_summary.routing_wire_length_map;
  double& total_wire_length = RTDM.getSummary().ta_summary.total_wire_length;
  std::map<int32_t, int32_t>& routing_violation_num_map = RTDM.getSummary().ta_summary.routing_violation_num_map;
  int32_t& total_violation_num = RTDM.getSummary().ta_summary.total_violation_num;

  for (RoutingLayer& routing_layer : routing_layer_list) {
    routing_wire_length_map[routing_layer.get_layer_idx()] = 0;
    routing_violation_num_map[routing_layer.get_layer_idx()] = 0;
  }
  total_wire_length = 0;
  total_violation_num = 0;

  for (auto& [net_idx, segment_set] : RTDM.getDetailedNetResultMap(die)) {
    for (Segment<LayerCoord>* segment : segment_set) {
      LayerCoord& first_coord = segment->get_first();
      LayerCoord& second_coord = segment->get_second();
      if (first_coord.get_layer_idx() == second_coord.get_layer_idx()) {
        double wire_length = RTUTIL.getManhattanDistance(first_coord, second_coord) / 1.0 / micron_dbu;
        routing_wire_length_map[first_coord.get_layer_idx()] += wire_length;
        total_wire_length += wire_length;
      }
    }
  }
  for (Violation* violation : RTDM.getViolationSet(die)) {
    routing_violation_num_map[violation->get_violation_shape().get_layer_idx()]++;
    total_violation_num++;
  }
}

void TrackAssigner::printSummary(TAModel& ta_model)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::map<int32_t, double>& routing_wire_length_map = RTDM.getSummary().ta_summary.routing_wire_length_map;
  double& total_wire_length = RTDM.getSummary().ta_summary.total_wire_length;
  std::map<int32_t, int32_t>& routing_violation_num_map = RTDM.getSummary().ta_summary.routing_violation_num_map;
  int32_t& total_violation_num = RTDM.getSummary().ta_summary.total_violation_num;

  fort::char_table routing_wire_length_map_table;
  {
    routing_wire_length_map_table << fort::header << "routing_layer" << "wire_length" << "proportion" << fort::endr;
    for (RoutingLayer& routing_layer : routing_layer_list) {
      routing_wire_length_map_table << routing_layer.get_layer_name() << routing_wire_length_map[routing_layer.get_layer_idx()]
                                    << RTUTIL.getPercentage(routing_wire_length_map[routing_layer.get_layer_idx()], total_wire_length)
                                    << fort::endr;
    }
    routing_wire_length_map_table << fort::header << "Total" << total_wire_length
                                  << RTUTIL.getPercentage(total_wire_length, total_wire_length) << fort::endr;
  }
  fort::char_table routing_violation_num_map_table;
  {
    routing_violation_num_map_table << fort::header << "routing_layer" << "violation_num" << "proportion" << fort::endr;
    for (RoutingLayer& routing_layer : routing_layer_list) {
      routing_violation_num_map_table << routing_layer.get_layer_name() << routing_violation_num_map[routing_layer.get_layer_idx()]
                                      << RTUTIL.getPercentage(routing_violation_num_map[routing_layer.get_layer_idx()], total_violation_num)
                                      << fort::endr;
    }
    routing_violation_num_map_table << fort::header << "Total" << total_violation_num
                                    << RTUTIL.getPercentage(total_violation_num, total_violation_num) << fort::endr;
  }
  std::vector<fort::char_table> table_list;
  table_list.push_back(routing_wire_length_map_table);
  table_list.push_back(routing_violation_num_map_table);
  RTUTIL.printTableList(table_list);
}

void TrackAssigner::writeNetCSV(TAModel& ta_model)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  GridMap<GCell>& gcell_map = RTDM.getDatabase().get_gcell_map();
  std::string& ta_temp_directory_path = RTDM.getConfig().ta_temp_directory_path;
  int32_t output_csv = RTDM.getConfig().output_csv;
  if (!output_csv) {
    return;
  }
  std::vector<GridMap<int32_t>> layer_net_map;
  layer_net_map.resize(routing_layer_list.size());
  for (GridMap<int32_t>& net_map : layer_net_map) {
    net_map.init(gcell_map.get_x_size(), gcell_map.get_y_size());
  }
  for (int32_t x = 0; x < gcell_map.get_x_size(); x++) {
    for (int32_t y = 0; y < gcell_map.get_y_size(); y++) {
      std::map<int32_t, std::set<int32_t>> net_layer_map;
      for (auto& [net_idx, segment_set] : gcell_map[x][y].get_detailed_net_result_map()) {
        for (Segment<LayerCoord>* segment : segment_set) {
          int32_t first_layer_idx = segment->get_first().get_layer_idx();
          int32_t second_layer_idx = segment->get_second().get_layer_idx();
          RTUTIL.swapByASC(first_layer_idx, second_layer_idx);
          for (int32_t layer_idx = first_layer_idx; layer_idx <= second_layer_idx; layer_idx++) {
            net_layer_map[net_idx].insert(layer_idx);
          }
        }
      }
      for (auto& [net_idx, layer_set] : net_layer_map) {
        for (int32_t layer_idx : layer_set) {
          layer_net_map[layer_idx][x][y]++;
        }
      }
    }
  }
  for (RoutingLayer& routing_layer : routing_layer_list) {
    std::ofstream* net_csv_file
        = RTUTIL.getOutputFileStream(RTUTIL.getString(ta_temp_directory_path, "net_map_", routing_layer.get_layer_name(), ".csv"));
    GridMap<int32_t>& net_map = layer_net_map[routing_layer.get_layer_idx()];
    for (int32_t y = net_map.get_y_size() - 1; y >= 0; y--) {
      for (int32_t x = 0; x < net_map.get_x_size(); x++) {
        RTUTIL.pushStream(net_csv_file, net_map[x][y], ",");
      }
      RTUTIL.pushStream(net_csv_file, "\n");
    }
    RTUTIL.closeFileStream(net_csv_file);
  }
}

void TrackAssigner::writeViolationCSV(TAModel& ta_model)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  GridMap<GCell>& gcell_map = RTDM.getDatabase().get_gcell_map();
  std::string& ta_temp_directory_path = RTDM.getConfig().ta_temp_directory_path;
  int32_t output_csv = RTDM.getConfig().output_csv;
  if (!output_csv) {
    return;
  }
  std::vector<GridMap<int32_t>> layer_violation_map;
  layer_violation_map.resize(routing_layer_list.size());
  for (GridMap<int32_t>& violation_map : layer_violation_map) {
    violation_map.init(gcell_map.get_x_size(), gcell_map.get_y_size());
  }
  for (int32_t x = 0; x < gcell_map.get_x_size(); x++) {
    for (int32_t y = 0; y < gcell_map.get_y_size(); y++) {
      for (Violation* violation : gcell_map[x][y].get_violation_set()) {
        layer_violation_map[violation->get_violation_shape().get_layer_idx()][x][y]++;
      }
    }
  }
  for (RoutingLayer& routing_layer : routing_layer_list) {
    std::ofstream* violation_csv_file
        = RTUTIL.getOutputFileStream(RTUTIL.getString(ta_temp_directory_path, "violation_map_", routing_layer.get_layer_name(), ".csv"));
    GridMap<int32_t>& violation_map = layer_violation_map[routing_layer.get_layer_idx()];
    for (int32_t y = violation_map.get_y_size() - 1; y >= 0; y--) {
      for (int32_t x = 0; x < violation_map.get_x_size(); x++) {
        RTUTIL.pushStream(violation_csv_file, violation_map[x][y], ",");
      }
      RTUTIL.pushStream(violation_csv_file, "\n");
    }
    RTUTIL.closeFileStream(violation_csv_file);
  }
}

#endif

#if 1  // debug

void TrackAssigner::debugPlotTAModel(TAModel& ta_model, std::string flag)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  Die& die = RTDM.getDatabase().get_die();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::string& ta_temp_directory_path = RTDM.getConfig().ta_temp_directory_path;

  GPGDS gp_gds;

  // gcell_axis
  {
    GPStruct gcell_axis_struct("gcell_axis");
    std::vector<int32_t> gcell_x_list = RTUTIL.getScaleList(die.get_real_ll_x(), die.get_real_ur_x(), gcell_axis.get_x_grid_list());
    std::vector<int32_t> gcell_y_list = RTUTIL.getScaleList(die.get_real_ll_y(), die.get_real_ur_y(), gcell_axis.get_y_grid_list());
    for (int32_t x : gcell_x_list) {
      GPPath gp_path;
      gp_path.set_layer_idx(0);
      gp_path.set_data_type(1);
      gp_path.set_segment(x, die.get_real_ll_y(), x, die.get_real_ur_y());
      gcell_axis_struct.push(gp_path);
    }
    for (int32_t y : gcell_y_list) {
      GPPath gp_path;
      gp_path.set_layer_idx(0);
      gp_path.set_data_type(1);
      gp_path.set_segment(die.get_real_ll_x(), y, die.get_real_ur_x(), y);
      gcell_axis_struct.push(gp_path);
    }
    gp_gds.addStruct(gcell_axis_struct);
  }

  // track_axis_struct
  {
    GPStruct track_axis_struct("track_axis_struct");
    for (RoutingLayer& routing_layer : routing_layer_list) {
      std::vector<int32_t> x_list = RTUTIL.getScaleList(die.get_real_ll_x(), die.get_real_ur_x(), routing_layer.getXTrackGridList());
      std::vector<int32_t> y_list = RTUTIL.getScaleList(die.get_real_ll_y(), die.get_real_ur_y(), routing_layer.getYTrackGridList());
      for (int32_t x : x_list) {
        GPPath gp_path;
        gp_path.set_data_type(static_cast<int32_t>(GPDataType::kAxis));
        gp_path.set_segment(x, die.get_real_ll_y(), x, die.get_real_ur_y());
        gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(routing_layer.get_layer_idx()));
        track_axis_struct.push(gp_path);
      }
      for (int32_t y : y_list) {
        GPPath gp_path;
        gp_path.set_data_type(static_cast<int32_t>(GPDataType::kAxis));
        gp_path.set_segment(die.get_real_ll_x(), y, die.get_real_ur_x(), y);
        gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(routing_layer.get_layer_idx()));
        track_axis_struct.push(gp_path);
      }
    }
    gp_gds.addStruct(track_axis_struct);
  }

  // fixed_rect
  for (auto& [is_routing, layer_net_fixed_rect_map] : RTDM.getTypeLayerNetFixedRectMap(die)) {
    for (auto& [layer_idx, net_fixed_rect_map] : layer_net_fixed_rect_map) {
      for (auto& [net_idx, fixed_rect_set] : net_fixed_rect_map) {
        GPStruct fixed_rect_struct(RTUTIL.getString("fixed_rect(net_", net_idx, ")"));
        for (auto& fixed_rect : fixed_rect_set) {
          GPBoundary gp_boundary;
          gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
          gp_boundary.set_rect(fixed_rect->get_real_rect());
          if (is_routing) {
            gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
          } else {
            gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(layer_idx));
          }
          fixed_rect_struct.push(gp_boundary);
        }
        gp_gds.addStruct(fixed_rect_struct);
      }
    }
  }

  // net_access_result
  for (auto& [net_idx, segment_set] : RTDM.getNetAccessResultMap(die)) {
    GPStruct access_result_struct(RTUTIL.getString("access_result(net_", net_idx, ")"));
    for (Segment<LayerCoord>* segment : segment_set) {
      for (NetShape& net_shape : RTDM.getNetShapeList(net_idx, *segment)) {
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
        gp_boundary.set_rect(net_shape.get_rect());
        if (net_shape.get_is_routing()) {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(net_shape.get_layer_idx()));
        } else {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(net_shape.get_layer_idx()));
        }
        access_result_struct.push(gp_boundary);
      }
    }
    gp_gds.addStruct(access_result_struct);
  }

  // access_point
  for (auto& [net_idx, access_point_set] : RTDM.getNetAccessPointMap(die)) {
    GPStruct access_point_struct(RTUTIL.getString("access_point(net_", net_idx, ")"));
    for (AccessPoint* access_point : access_point_set) {
      int32_t x = access_point->get_real_x();
      int32_t y = access_point->get_real_y();

      GPBoundary access_point_boundary;
      access_point_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(access_point->get_layer_idx()));
      access_point_boundary.set_data_type(static_cast<int32_t>(GPDataType::kAccessPoint));
      access_point_boundary.set_rect(x - 10, y - 10, x + 10, y + 10);
      access_point_struct.push(access_point_boundary);
    }
    gp_gds.addStruct(access_point_struct);
  }

  // routing result
  for (auto& [net_idx, segment_set] : RTDM.getDetailedNetResultMap(die)) {
    GPStruct detailed_result_struct(RTUTIL.getString("detailed_result(net_", net_idx, ")"));
    for (Segment<LayerCoord>* segment : segment_set) {
      for (NetShape& net_shape : RTDM.getNetShapeList(net_idx, *segment)) {
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
        gp_boundary.set_rect(net_shape.get_rect());
        if (net_shape.get_is_routing()) {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(net_shape.get_layer_idx()));
        } else {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(net_shape.get_layer_idx()));
        }
        detailed_result_struct.push(gp_boundary);
      }
    }
    gp_gds.addStruct(detailed_result_struct);
  }

  // violation
  {
    GPStruct violation_struct("violation");
    for (Violation* violation : RTDM.getViolationSet(die)) {
      EXTLayerRect& violation_shape = violation->get_violation_shape();

      GPBoundary gp_boundary;
      gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kViolation));
      gp_boundary.set_rect(violation_shape.get_real_rect());
      if (violation->get_is_routing()) {
        gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(violation_shape.get_layer_idx()));
      } else {
        gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(violation_shape.get_layer_idx()));
      }
      violation_struct.push(gp_boundary);
    }
    gp_gds.addStruct(violation_struct);
  }

  std::string gds_file_path = RTUTIL.getString(ta_temp_directory_path, flag, "_ta_model.gds");
  RTGP.plot(gp_gds, gds_file_path);
}

void TrackAssigner::debugCheckTAPanel(TAPanel& ta_panel)
{
  TAPanelId& ta_panel_id = ta_panel.get_ta_panel_id();
  if (ta_panel.get_ta_panel_id().get_layer_idx() < 0 || ta_panel.get_ta_panel_id().get_panel_idx() < 0) {
    RTLOG.error(Loc::current(), "The ta_panel_id is error!");
  }

  GridMap<TANode>& ta_node_map = ta_panel.get_ta_node_map();
  for (int32_t x = 0; x < ta_node_map.get_x_size(); x++) {
    for (int32_t y = 0; y < ta_node_map.get_y_size(); y++) {
      TANode& ta_node = ta_node_map[x][y];
      if (!RTUTIL.isInside(ta_panel.get_panel_rect().get_real_rect(), ta_node.get_planar_coord())) {
        RTLOG.error(Loc::current(), "The ta_node is out of panel!");
      }
      for (auto& [orient, neighbor] : ta_node.get_neighbor_node_map()) {
        Orientation opposite_orient = RTUTIL.getOppositeOrientation(orient);
        if (!RTUTIL.exist(neighbor->get_neighbor_node_map(), opposite_orient)) {
          RTLOG.error(Loc::current(), "The ta_node neighbor is not bidirectional!");
        }
        if (neighbor->get_neighbor_node_map()[opposite_orient] != &ta_node) {
          RTLOG.error(Loc::current(), "The ta_node neighbor is not bidirectional!");
        }
        LayerCoord node_coord(ta_node.get_planar_coord(), ta_node.get_layer_idx());
        LayerCoord neighbor_coord(neighbor->get_planar_coord(), neighbor->get_layer_idx());
        if (RTUTIL.getOrientation(node_coord, neighbor_coord) == orient) {
          continue;
        }
        RTLOG.error(Loc::current(), "The neighbor orient is different with real region!");
      }
    }
  }

  for (TATask* ta_task : ta_panel.get_ta_task_list()) {
    if (ta_task->get_net_idx() < 0) {
      RTLOG.error(Loc::current(), "The idx of origin net is illegal!");
    }
    for (TAGroup& ta_group : ta_task->get_ta_group_list()) {
      if (ta_group.get_coord_list().empty()) {
        RTLOG.error(Loc::current(), "The coord_direction_map is empty!");
      }
      for (LayerCoord& coord : ta_group.get_coord_list()) {
        int32_t layer_idx = coord.get_layer_idx();
        if (layer_idx != ta_panel.get_panel_rect().get_layer_idx()) {
          RTLOG.error(Loc::current(), "The layer idx of group coord is illegal!");
        }
        if (!RTUTIL.existTrackGrid(coord, ta_panel.get_panel_track_axis())) {
          RTLOG.error(Loc::current(), "There is no grid coord for real coord(", coord.get_x(), ",", coord.get_y(), ")!");
        }
        PlanarCoord grid_coord = RTUTIL.getTrackGrid(coord, ta_panel.get_panel_track_axis());
        TANode& ta_node = ta_node_map[grid_coord.get_x()][grid_coord.get_y()];
        if (ta_node.get_neighbor_node_map().empty()) {
          RTLOG.error(Loc::current(), "The neighbor of group coord (", coord.get_x(), ",", coord.get_y(), ",", layer_idx,
                      ") is empty in panel(", ta_panel_id.get_layer_idx(), ",", ta_panel_id.get_panel_idx(), ")");
        }
        if (RTUTIL.isInside(ta_panel.get_panel_rect().get_real_rect(), coord)) {
          continue;
        }
        RTLOG.error(Loc::current(), "The coord (", coord.get_x(), ",", coord.get_y(), ") is out of panel!");
      }
    }
  }
}

void TrackAssigner::debugPlotTAPanel(TAPanel& ta_panel, int32_t curr_task_idx, std::string flag)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<std::vector<ViaMaster>>& layer_via_master_list = RTDM.getDatabase().get_layer_via_master_list();
  std::string& ta_temp_directory_path = RTDM.getConfig().ta_temp_directory_path;

  PlanarRect panel_rect = ta_panel.get_panel_rect().get_real_rect();

  int32_t width = INT32_MAX;
  for (ScaleGrid& x_grid : ta_panel.get_panel_track_axis().get_x_grid_list()) {
    width = std::min(width, x_grid.get_step_length());
  }
  for (ScaleGrid& y_grid : ta_panel.get_panel_track_axis().get_y_grid_list()) {
    width = std::min(width, y_grid.get_step_length());
  }
  width = std::max(1, width / 3);

  GPGDS gp_gds;

  // base_region
  {
    GPStruct base_region_struct("base_region");
    GPBoundary gp_boundary;
    gp_boundary.set_layer_idx(0);
    gp_boundary.set_data_type(0);
    gp_boundary.set_rect(panel_rect);
    base_region_struct.push(gp_boundary);
    gp_gds.addStruct(base_region_struct);
  }

  // gcell_axis
  {
    GPStruct gcell_axis_struct("gcell_axis");
    std::vector<int32_t> gcell_x_list = RTUTIL.getScaleList(panel_rect.get_ll_x(), panel_rect.get_ur_x(), gcell_axis.get_x_grid_list());
    std::vector<int32_t> gcell_y_list = RTUTIL.getScaleList(panel_rect.get_ll_y(), panel_rect.get_ur_y(), gcell_axis.get_y_grid_list());
    for (int32_t x : gcell_x_list) {
      GPPath gp_path;
      gp_path.set_layer_idx(0);
      gp_path.set_data_type(1);
      gp_path.set_segment(x, panel_rect.get_ll_y(), x, panel_rect.get_ur_y());
      gcell_axis_struct.push(gp_path);
    }
    for (int32_t y : gcell_y_list) {
      GPPath gp_path;
      gp_path.set_layer_idx(0);
      gp_path.set_data_type(1);
      gp_path.set_segment(panel_rect.get_ll_x(), y, panel_rect.get_ur_x(), y);
      gcell_axis_struct.push(gp_path);
    }
    gp_gds.addStruct(gcell_axis_struct);
  }
  GridMap<TANode>& ta_node_map = ta_panel.get_ta_node_map();

  // ta_node_map
  {
    GPStruct ta_node_map_struct("ta_node_map");
    for (int32_t grid_x = 0; grid_x < ta_node_map.get_x_size(); grid_x++) {
      for (int32_t grid_y = 0; grid_y < ta_node_map.get_y_size(); grid_y++) {
        TANode& ta_node = ta_node_map[grid_x][grid_y];
        PlanarRect real_rect = RTUTIL.getEnlargedRect(ta_node.get_planar_coord(), width);
        int32_t y_reduced_span = std::max(1, real_rect.getYSpan() / 12);
        int32_t y = real_rect.get_ur_y();

        GPBoundary gp_boundary;
        switch (ta_node.get_state()) {
          case TANodeState::kNone:
            gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kNone));
            break;
          case TANodeState::kOpen:
            gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kOpen));
            break;
          case TANodeState::kClose:
            gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kClose));
            break;
          default:
            RTLOG.error(Loc::current(), "The type is error!");
            break;
        }
        gp_boundary.set_rect(real_rect);
        gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(ta_node.get_layer_idx()));
        ta_node_map_struct.push(gp_boundary);

        y -= y_reduced_span;
        GPText gp_text_node_real_coord;
        gp_text_node_real_coord.set_coord(real_rect.get_ll_x(), y);
        gp_text_node_real_coord.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
        gp_text_node_real_coord.set_message(
            RTUTIL.getString("(", ta_node.get_x(), " , ", ta_node.get_y(), " , ", ta_node.get_layer_idx(), ")"));
        gp_text_node_real_coord.set_layer_idx(RTGP.getGDSIdxByRouting(ta_node.get_layer_idx()));
        gp_text_node_real_coord.set_presentation(GPTextPresentation::kLeftMiddle);
        ta_node_map_struct.push(gp_text_node_real_coord);

        y -= y_reduced_span;
        GPText gp_text_node_grid_coord;
        gp_text_node_grid_coord.set_coord(real_rect.get_ll_x(), y);
        gp_text_node_grid_coord.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
        gp_text_node_grid_coord.set_message(RTUTIL.getString("(", grid_x, " , ", grid_y, " , ", ta_node.get_layer_idx(), ")"));
        gp_text_node_grid_coord.set_layer_idx(RTGP.getGDSIdxByRouting(ta_node.get_layer_idx()));
        gp_text_node_grid_coord.set_presentation(GPTextPresentation::kLeftMiddle);
        ta_node_map_struct.push(gp_text_node_grid_coord);

        y -= y_reduced_span;
        GPText gp_text_orient_fixed_rect_map;
        gp_text_orient_fixed_rect_map.set_coord(real_rect.get_ll_x(), y);
        gp_text_orient_fixed_rect_map.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
        gp_text_orient_fixed_rect_map.set_message("orient_fixed_rect_map: ");
        gp_text_orient_fixed_rect_map.set_layer_idx(RTGP.getGDSIdxByRouting(ta_node.get_layer_idx()));
        gp_text_orient_fixed_rect_map.set_presentation(GPTextPresentation::kLeftMiddle);
        ta_node_map_struct.push(gp_text_orient_fixed_rect_map);

        if (!ta_node.get_orient_fixed_rect_map().empty()) {
          y -= y_reduced_span;
          GPText gp_text_orient_fixed_rect_map_info;
          gp_text_orient_fixed_rect_map_info.set_coord(real_rect.get_ll_x(), y);
          gp_text_orient_fixed_rect_map_info.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
          std::string orient_fixed_rect_map_info_message = "--";
          for (auto& [orient, net_set] : ta_node.get_orient_fixed_rect_map()) {
            orient_fixed_rect_map_info_message += RTUTIL.getString("(", GetOrientationName()(orient));
            for (int32_t net_idx : net_set) {
              orient_fixed_rect_map_info_message += RTUTIL.getString(",", net_idx);
            }
            orient_fixed_rect_map_info_message += RTUTIL.getString(")");
          }
          gp_text_orient_fixed_rect_map_info.set_message(orient_fixed_rect_map_info_message);
          gp_text_orient_fixed_rect_map_info.set_layer_idx(RTGP.getGDSIdxByRouting(ta_node.get_layer_idx()));
          gp_text_orient_fixed_rect_map_info.set_presentation(GPTextPresentation::kLeftMiddle);
          ta_node_map_struct.push(gp_text_orient_fixed_rect_map_info);
        }

        y -= y_reduced_span;
        GPText gp_text_orient_routed_rect_map;
        gp_text_orient_routed_rect_map.set_coord(real_rect.get_ll_x(), y);
        gp_text_orient_routed_rect_map.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
        gp_text_orient_routed_rect_map.set_message("orient_routed_rect_map: ");
        gp_text_orient_routed_rect_map.set_layer_idx(RTGP.getGDSIdxByRouting(ta_node.get_layer_idx()));
        gp_text_orient_routed_rect_map.set_presentation(GPTextPresentation::kLeftMiddle);
        ta_node_map_struct.push(gp_text_orient_routed_rect_map);

        if (!ta_node.get_orient_routed_rect_map().empty()) {
          y -= y_reduced_span;
          GPText gp_text_orient_routed_rect_map_info;
          gp_text_orient_routed_rect_map_info.set_coord(real_rect.get_ll_x(), y);
          gp_text_orient_routed_rect_map_info.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
          std::string orient_routed_rect_map_info_message = "--";
          for (auto& [orient, net_set] : ta_node.get_orient_routed_rect_map()) {
            orient_routed_rect_map_info_message += RTUTIL.getString("(", GetOrientationName()(orient));
            for (int32_t net_idx : net_set) {
              orient_routed_rect_map_info_message += RTUTIL.getString(",", net_idx);
            }
            orient_routed_rect_map_info_message += RTUTIL.getString(")");
          }
          gp_text_orient_routed_rect_map_info.set_message(orient_routed_rect_map_info_message);
          gp_text_orient_routed_rect_map_info.set_layer_idx(RTGP.getGDSIdxByRouting(ta_node.get_layer_idx()));
          gp_text_orient_routed_rect_map_info.set_presentation(GPTextPresentation::kLeftMiddle);
          ta_node_map_struct.push(gp_text_orient_routed_rect_map_info);
        }

        y -= y_reduced_span;
        GPText gp_text_orient_violation_number_map;
        gp_text_orient_violation_number_map.set_coord(real_rect.get_ll_x(), y);
        gp_text_orient_violation_number_map.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
        gp_text_orient_violation_number_map.set_message("orient_violation_number_map: ");
        gp_text_orient_violation_number_map.set_layer_idx(RTGP.getGDSIdxByRouting(ta_node.get_layer_idx()));
        gp_text_orient_violation_number_map.set_presentation(GPTextPresentation::kLeftMiddle);
        ta_node_map_struct.push(gp_text_orient_violation_number_map);

        if (!ta_node.get_orient_violation_number_map().empty()) {
          y -= y_reduced_span;
          GPText gp_text_orient_violation_number_map_info;
          gp_text_orient_violation_number_map_info.set_coord(real_rect.get_ll_x(), y);
          gp_text_orient_violation_number_map_info.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
          std::string orient_violation_number_map_info_message = "--";
          for (auto& [orient, violation_number] : ta_node.get_orient_violation_number_map()) {
            orient_violation_number_map_info_message
                += RTUTIL.getString("(", GetOrientationName()(orient), ",", violation_number != 0, ")");
          }
          gp_text_orient_violation_number_map_info.set_message(orient_violation_number_map_info_message);
          gp_text_orient_violation_number_map_info.set_layer_idx(RTGP.getGDSIdxByRouting(ta_node.get_layer_idx()));
          gp_text_orient_violation_number_map_info.set_presentation(GPTextPresentation::kLeftMiddle);
          ta_node_map_struct.push(gp_text_orient_violation_number_map_info);
        }

        y -= y_reduced_span;
        GPText gp_text_direction_set;
        gp_text_direction_set.set_coord(real_rect.get_ll_x(), y);
        gp_text_direction_set.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
        gp_text_direction_set.set_message("direction_set: ");
        gp_text_direction_set.set_layer_idx(RTGP.getGDSIdxByRouting(ta_node.get_layer_idx()));
        gp_text_direction_set.set_presentation(GPTextPresentation::kLeftMiddle);
        ta_node_map_struct.push(gp_text_direction_set);

        if (!ta_node.get_direction_set().empty()) {
          y -= y_reduced_span;
          GPText gp_text_direction_set_info;
          gp_text_direction_set_info.set_coord(real_rect.get_ll_x(), y);
          gp_text_direction_set_info.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
          std::string direction_set_info_message = "--";
          for (Direction direction : ta_node.get_direction_set()) {
            direction_set_info_message += RTUTIL.getString("(", GetDirectionName()(direction), ")");
          }
          gp_text_direction_set_info.set_message(direction_set_info_message);
          gp_text_direction_set_info.set_layer_idx(RTGP.getGDSIdxByRouting(ta_node.get_layer_idx()));
          gp_text_direction_set_info.set_presentation(GPTextPresentation::kLeftMiddle);
          ta_node_map_struct.push(gp_text_direction_set_info);
        }
      }
    }
    gp_gds.addStruct(ta_node_map_struct);
  }

  // neighbor_map
  {
    GPStruct neighbor_map_struct("neighbor_map");
    for (int32_t grid_x = 0; grid_x < ta_node_map.get_x_size(); grid_x++) {
      for (int32_t grid_y = 0; grid_y < ta_node_map.get_y_size(); grid_y++) {
        TANode& ta_node = ta_node_map[grid_x][grid_y];
        PlanarRect real_rect = RTUTIL.getEnlargedRect(ta_node.get_planar_coord(), width);

        int32_t ll_x = real_rect.get_ll_x();
        int32_t ll_y = real_rect.get_ll_y();
        int32_t ur_x = real_rect.get_ur_x();
        int32_t ur_y = real_rect.get_ur_y();
        int32_t mid_x = (ll_x + ur_x) / 2;
        int32_t mid_y = (ll_y + ur_y) / 2;
        int32_t x_reduced_span = (ur_x - ll_x) / 4;
        int32_t y_reduced_span = (ur_y - ll_y) / 4;
        int32_t width = std::min(x_reduced_span, y_reduced_span) / 2;

        for (auto& [orientation, neighbor_node] : ta_node.get_neighbor_node_map()) {
          GPPath gp_path;
          switch (orientation) {
            case Orientation::kEast:
              gp_path.set_segment(ur_x - x_reduced_span, mid_y, ur_x, mid_y);
              break;
            case Orientation::kSouth:
              gp_path.set_segment(mid_x, ll_y, mid_x, ll_y + y_reduced_span);
              break;
            case Orientation::kWest:
              gp_path.set_segment(ll_x, mid_y, ll_x + x_reduced_span, mid_y);
              break;
            case Orientation::kNorth:
              gp_path.set_segment(mid_x, ur_y - y_reduced_span, mid_x, ur_y);
              break;
            case Orientation::kAbove:
              gp_path.set_segment(ur_x - x_reduced_span, ur_y - y_reduced_span, ur_x, ur_y);
              break;
            case Orientation::kBelow:
              gp_path.set_segment(ll_x, ll_y, ll_x + x_reduced_span, ll_y + y_reduced_span);
              break;
            default:
              RTLOG.error(Loc::current(), "The orientation is oblique!");
              break;
          }
          gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(ta_node.get_layer_idx()));
          gp_path.set_width(width);
          gp_path.set_data_type(static_cast<int32_t>(GPDataType::kNeighbor));
          neighbor_map_struct.push(gp_path);
        }
      }
    }
    gp_gds.addStruct(neighbor_map_struct);
  }

  // panel_track_axis
  {
    GPStruct panel_track_axis_struct("panel_track_axis");
    PlanarCoord& real_ll = panel_rect.get_ll();
    PlanarCoord& real_ur = panel_rect.get_ur();
    ScaleAxis& panel_track_axis = ta_panel.get_panel_track_axis();
    std::vector<int32_t> x_list = RTUTIL.getScaleList(real_ll.get_x(), real_ur.get_x(), panel_track_axis.get_x_grid_list());
    std::vector<int32_t> y_list = RTUTIL.getScaleList(real_ll.get_y(), real_ur.get_y(), panel_track_axis.get_y_grid_list());

    for (int32_t x : x_list) {
      GPPath gp_path;
      gp_path.set_data_type(static_cast<int32_t>(GPDataType::kAxis));
      gp_path.set_segment(x, real_ll.get_y(), x, real_ur.get_y());
      gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(ta_panel.get_panel_rect().get_layer_idx()));
      panel_track_axis_struct.push(gp_path);
    }
    for (int32_t y : y_list) {
      GPPath gp_path;
      gp_path.set_data_type(static_cast<int32_t>(GPDataType::kAxis));
      gp_path.set_segment(real_ll.get_x(), y, real_ur.get_x(), y);
      gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(ta_panel.get_panel_rect().get_layer_idx()));
      panel_track_axis_struct.push(gp_path);
    }
    gp_gds.addStruct(panel_track_axis_struct);
  }

  // fixed_rect
  for (auto& [is_routing, layer_net_fixed_rect_map] : ta_panel.get_type_layer_net_fixed_rect_map()) {
    for (auto& [layer_idx, net_fixed_rect_map] : layer_net_fixed_rect_map) {
      if (is_routing != true || layer_idx != ta_panel.get_panel_rect().get_layer_idx()) {
        continue;
      }
      for (auto& [net_idx, fixed_rect_set] : net_fixed_rect_map) {
        GPStruct fixed_rect_struct(RTUTIL.getString("fixed_rect(net_", net_idx, ")"));
        for (auto& fixed_rect : fixed_rect_set) {
          GPBoundary gp_boundary;
          gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
          gp_boundary.set_rect(fixed_rect->get_real_rect());
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(ta_panel.get_panel_rect().get_layer_idx()));
          fixed_rect_struct.push(gp_boundary);
        }
        gp_gds.addStruct(fixed_rect_struct);
      }
      break;
    }
  }

  // net_access_result
  for (auto& [net_idx, segment_set] : ta_panel.get_net_access_result_map()) {
    GPStruct access_result_struct(RTUTIL.getString("access_result(net_", net_idx, ")"));
    for (Segment<LayerCoord>* segment : segment_set) {
      for (NetShape& net_shape : RTDM.getNetShapeList(net_idx, *segment)) {
        if (net_shape.get_is_routing() != true || net_shape.get_layer_idx() != ta_panel.get_panel_rect().get_layer_idx()) {
          continue;
        }
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
        gp_boundary.set_rect(net_shape.get_rect());
        if (net_shape.get_is_routing()) {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(net_shape.get_layer_idx()));
        } else {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(net_shape.get_layer_idx()));
        }
        access_result_struct.push(gp_boundary);
      }
    }
    gp_gds.addStruct(access_result_struct);
  }

  // task
  for (TATask* ta_task : ta_panel.get_ta_task_list()) {
    GPStruct task_struct(RTUTIL.getString("task(net_", ta_task->get_net_idx(), ")"));

    if (curr_task_idx == -1 || ta_task->get_net_idx() == curr_task_idx) {
      for (TAGroup& ta_group : ta_task->get_ta_group_list()) {
        for (LayerCoord& coord : ta_group.get_coord_list()) {
          GPBoundary gp_boundary;
          gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kKey));
          gp_boundary.set_rect(RTUTIL.getEnlargedRect(coord, width));
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(coord.get_layer_idx()));
          task_struct.push(gp_boundary);
        }
      }
    }
    {
      // bounding_box
      GPBoundary gp_boundary;
      gp_boundary.set_layer_idx(0);
      gp_boundary.set_data_type(2);
      gp_boundary.set_rect(ta_task->get_bounding_box());
      task_struct.push(gp_boundary);
    }
    for (Segment<LayerCoord>& segment : ta_panel.get_net_task_result_map()[ta_task->get_net_idx()][ta_task->get_task_idx()]) {
      LayerCoord first_coord = segment.get_first();
      LayerCoord second_coord = segment.get_second();
      int32_t first_layer_idx = first_coord.get_layer_idx();
      int32_t second_layer_idx = second_coord.get_layer_idx();
      int32_t half_width = routing_layer_list[first_layer_idx].get_min_width() / 2;

      if (first_layer_idx == second_layer_idx) {
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kPath));
        gp_boundary.set_rect(RTUTIL.getEnlargedRect(first_coord, second_coord, half_width));
        gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(first_layer_idx));
        task_struct.push(gp_boundary);
      } else {
        RTUTIL.swapByASC(first_layer_idx, second_layer_idx);
        for (int32_t layer_idx = first_layer_idx; layer_idx < second_layer_idx; layer_idx++) {
          ViaMaster& via_master = layer_via_master_list[layer_idx].front();

          LayerRect& above_enclosure = via_master.get_above_enclosure();
          LayerRect offset_above_enclosure(RTUTIL.getOffsetRect(above_enclosure, first_coord), above_enclosure.get_layer_idx());
          GPBoundary above_boundary;
          above_boundary.set_rect(offset_above_enclosure);
          above_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(above_enclosure.get_layer_idx()));
          above_boundary.set_data_type(static_cast<int32_t>(GPDataType::kPath));
          task_struct.push(above_boundary);

          LayerRect& below_enclosure = via_master.get_below_enclosure();
          LayerRect offset_below_enclosure(RTUTIL.getOffsetRect(below_enclosure, first_coord), below_enclosure.get_layer_idx());
          GPBoundary below_boundary;
          below_boundary.set_rect(offset_below_enclosure);
          below_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(below_enclosure.get_layer_idx()));
          below_boundary.set_data_type(static_cast<int32_t>(GPDataType::kPath));
          task_struct.push(below_boundary);

          for (PlanarRect& cut_shape : via_master.get_cut_shape_list()) {
            LayerRect offset_cut_shape(RTUTIL.getOffsetRect(cut_shape, first_coord), via_master.get_cut_layer_idx());
            GPBoundary cut_boundary;
            cut_boundary.set_rect(offset_cut_shape);
            cut_boundary.set_layer_idx(RTGP.getGDSIdxByCut(via_master.get_cut_layer_idx()));
            cut_boundary.set_data_type(static_cast<int32_t>(GPDataType::kPath));
            task_struct.push(cut_boundary);
          }
        }
      }
    }
    gp_gds.addStruct(task_struct);
  }

  // violation
  {
    GPStruct violation_struct("violation");
    for (Violation& violation : ta_panel.get_violation_list()) {
      EXTLayerRect& violation_shape = violation.get_violation_shape();

      GPBoundary gp_boundary;
      gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kViolation));
      gp_boundary.set_rect(violation_shape.get_real_rect());
      if (violation.get_is_routing()) {
        gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(violation_shape.get_layer_idx()));
      } else {
        gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(violation_shape.get_layer_idx()));
      }
      violation_struct.push(gp_boundary);
    }
    gp_gds.addStruct(violation_struct);
  }

  std::string gds_file_path = RTUTIL.getString(ta_temp_directory_path, flag, "_ta_panel_", ta_panel.get_ta_panel_id().get_layer_idx(), "_",
                                               ta_panel.get_ta_panel_id().get_panel_idx(), ".gds");
  RTGP.plot(gp_gds, gds_file_path);
}

#endif

}  // namespace irt
