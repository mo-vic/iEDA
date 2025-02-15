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
#pragma once

#include "ChangeType.hpp"
#include "Config.hpp"
#include "DataManager.hpp"
#include "Database.hpp"
#include "Net.hpp"
#include "PABoxId.hpp"
#include "PAModel.hpp"
#include "PANet.hpp"
#include "PANode.hpp"
#include "PAParameter.hpp"
#include "RTHeader.hpp"

namespace irt {

#define RTPA (irt::PinAccessor::getInst())

class PinAccessor
{
 public:
  static void initInst();
  static PinAccessor& getInst();
  static void destroyInst();
  // function
  void access();

 private:
  // self
  static PinAccessor* _pa_instance;

  PinAccessor() = default;
  PinAccessor(const PinAccessor& other) = delete;
  PinAccessor(PinAccessor&& other) = delete;
  ~PinAccessor() = default;
  PinAccessor& operator=(const PinAccessor& other) = delete;
  PinAccessor& operator=(PinAccessor&& other) = delete;
  // function
  PAModel initPAModel();
  std::vector<PANet> convertToPANetList(std::vector<Net>& net_list);
  PANet convertToPANet(Net& net);
  void initAccessPointList(PAModel& pa_model);
  std::vector<LayerRect> getLegalShapeList(PAModel& pa_model, int32_t net_idx, Pin* pin);
  std::vector<PlanarRect> getPlanarLegalRectList(PAModel& pa_model, int32_t curr_net_idx, std::vector<EXTLayerRect>& pin_shape_list);
  std::vector<AccessPoint> getAccessPointListByTrackGrid(int32_t pin_idx, std::vector<LayerRect>& legal_shape_list);
  std::vector<AccessPoint> getAccessPointListByOnTrack(int32_t pin_idx, std::vector<LayerRect>& legal_shape_list);
  std::vector<AccessPoint> getAccessPointListByShapeCenter(int32_t pin_idx, std::vector<LayerRect>& legal_shape_list);
  void uploadAccessPointList(PAModel& pa_model);
  void setPAParameter(PAModel& pa_model);
  void initPABoxMap(PAModel& pa_model);
  void buildBoxSchedule(PAModel& pa_model);
  void routePABoxMap(PAModel& pa_model);
  void buildFixedRect(PABox& pa_box);
  void buildAccessResult(PABox& pa_box);
  void initPATaskList(PAModel& pa_model, PABox& pa_box);
  bool needRouting(PABox& pa_box);
  void buildBoxTrackAxis(PABox& pa_box);
  void buildLayerNodeMap(PABox& pa_box);
  void buildPANodeNeighbor(PABox& pa_box);
  void buildOrientNetMap(PABox& pa_box);
  void routePABox(PABox& pa_box);
  std::vector<PATask*> initTaskSchedule(PABox& pa_box);
  std::vector<PATask*> getTaskScheduleByViolation(PABox& pa_box);
  void routePATask(PABox& pa_box, PATask* pa_task);
  void initSingleTask(PABox& pa_box, PATask* pa_task);
  bool isConnectedAllEnd(PABox& pa_box);
  void routeSinglePath(PABox& pa_box);
  void initPathHead(PABox& pa_box);
  bool searchEnded(PABox& pa_box);
  void expandSearching(PABox& pa_box);
  void resetPathHead(PABox& pa_box);
  bool isRoutingFailed(PABox& pa_box);
  void resetSinglePath(PABox& pa_box);
  void updatePathResult(PABox& pa_box);
  std::vector<Segment<LayerCoord>> getRoutingSegmentListByNode(PANode* node);
  void updateDirectionSet(PABox& pa_box);
  void resetStartAndEnd(PABox& pa_box);
  void updateTaskResult(PABox& pa_box);
  std::vector<Segment<LayerCoord>> getRoutingSegmentList(PABox& pa_box);
  void resetSingleTask(PABox& pa_box);
  void pushToOpenList(PABox& pa_box, PANode* curr_node);
  PANode* popFromOpenList(PABox& pa_box);
  double getKnowCost(PABox& pa_box, PANode* start_node, PANode* end_node);
  double getNodeCost(PABox& pa_box, PANode* curr_node, Orientation orientation);
  double getKnowWireCost(PABox& pa_box, PANode* start_node, PANode* end_node);
  double getKnowCornerCost(PABox& pa_box, PANode* start_node, PANode* end_node);
  double getKnowViaCost(PABox& pa_box, PANode* start_node, PANode* end_node);
  double getEstimateCostToEnd(PABox& pa_box, PANode* curr_node);
  double getEstimateCost(PABox& pa_box, PANode* start_node, PANode* end_node);
  double getEstimateWireCost(PABox& pa_box, PANode* start_node, PANode* end_node);
  double getEstimateCornerCost(PABox& pa_box, PANode* start_node, PANode* end_node);
  double getEstimateViaCost(PABox& pa_box, PANode* start_node, PANode* end_node);
  void updateViolationList(PABox& pa_box);
  std::vector<Violation> getViolationList(PABox& pa_box);
  void uploadAccessResult(PABox& pa_box);
  void uploadViolation(PABox& pa_box);
  void freePABox(PABox& pa_box);
  int32_t getViolationNum();
  void updatePAModel(PAModel& pa_model);

#if 1  // update env
  void updateFixedRectToGraph(PABox& pa_box, ChangeType change_type, int32_t net_idx, EXTLayerRect* fixed_rect, bool is_routing);
  void updateFixedRectToGraph(PABox& pa_box, ChangeType change_type, int32_t net_idx, Segment<LayerCoord>& segment);
  void updateNetResultToGraph(PABox& pa_box, ChangeType change_type, int32_t net_idx, Segment<LayerCoord>& segment);
  void updateViolationToGraph(PABox& pa_box, ChangeType change_type, Violation& violation);
  std::map<PANode*, std::set<Orientation>> getNodeOrientationMap(PABox& pa_box, NetShape& net_shape);
  std::map<PANode*, std::set<Orientation>> getRoutingNodeOrientationMap(PABox& pa_box, NetShape& net_shape);
  std::map<PANode*, std::set<Orientation>> getCutNodeOrientationMap(PABox& pa_box, NetShape& net_shape);
#endif

#if 1  // exhibit
  void updateSummary(PAModel& pa_model);
  void printSummary(PAModel& pa_model);
  void writePlanarPinCSV(PAModel& pa_model);
  void writeLayerPinCSV(PAModel& pa_model);
#endif

#if 1  // debug
  void debugPlotPAModel(PAModel& pa_model, std::string flag);
  void debugCheckPABox(PABox& pa_box);
  void debugPlotPABox(PABox& pa_box, int32_t curr_task_idx, std::string flag);
#endif
};

}  // namespace irt
