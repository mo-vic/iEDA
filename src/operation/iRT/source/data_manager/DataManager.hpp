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
#include "Database.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "NetShape.hpp"
#include "SortStatus.hpp"
#include "Summary.hpp"

namespace irt {

#define RTDM (irt::DataManager::getInst())

class DataManager
{
 public:
  static void initInst();
  static DataManager& getInst();
  static void destroyInst();
  // function
  void input(std::map<std::string, std::any>& config_map);
  void output();

#if 1  // 更新GCellMap
  void updateFixedRectToGCellMap(ChangeType change_type, int32_t net_idx, EXTLayerRect* ext_layer_rect, bool is_routing);
  void updateAccessNetPointToGCellMap(ChangeType change_type, int32_t net_idx, AccessPoint* access_point);
  void updateNetAccessResultToGCellMap(ChangeType change_type, int32_t net_idx, Segment<LayerCoord>* segment);
  void updateGlobalNetResultToGCellMap(ChangeType change_type, int32_t net_idx, Segment<LayerCoord>* segment);
  void updateDetailedNetResultToGCellMap(ChangeType change_type, int32_t net_idx, Segment<LayerCoord>* segment);
  void updateNetPatchToGCellMap(ChangeType change_type, int32_t net_idx, EXTLayerRect* ext_layer_rect);
  void updateViolationToGCellMap(ChangeType change_type, Violation* violation);
  std::map<bool, std::map<int32_t, std::map<int32_t, std::set<EXTLayerRect*>>>> getTypeLayerNetFixedRectMap(EXTPlanarRect& region);
  std::map<int32_t, std::set<AccessPoint*>> getNetAccessPointMap(EXTPlanarRect& region);
  std::map<int32_t, std::set<Segment<LayerCoord>*>> getNetAccessResultMap(EXTPlanarRect& region);
  std::map<int32_t, std::set<Segment<LayerCoord>*>> getGlobalNetResultMap(EXTPlanarRect& region);
  std::map<int32_t, std::set<Segment<LayerCoord>*>> getDetailedNetResultMap(EXTPlanarRect& region);
  std::map<int32_t, std::set<EXTLayerRect*>> getNetPatchMap(EXTPlanarRect& region);
  std::set<Violation*> getViolationSet(EXTPlanarRect& region);
#endif

#if 1  // 获得NetShapeList
  std::vector<NetShape> getNetShapeList(int32_t net_idx, std::vector<Segment<LayerCoord>>& segment_list);
  std::vector<NetShape> getNetShapeList(int32_t net_idx, Segment<LayerCoord>& segment);
  std::vector<NetShape> getNetShapeList(int32_t net_idx, MTree<LayerCoord>& coord_tree);
  std::vector<NetShape> getNetShapeList(int32_t net_idx, LayerCoord& first_coord, LayerCoord& second_coord);
#endif

  Config& getConfig() { return _config; }
  Database& getDatabase() { return _database; }
  Summary& getSummary() { return _summary; }

 private:
  static DataManager* _dm_instance;
  // config & database & summary
  Config _config;
  Database _database;
  Summary _summary;

  DataManager() = default;
  DataManager(const DataManager& other) = delete;
  DataManager(DataManager&& other) = delete;
  ~DataManager()
  {
    Die& die = _database.get_die();

    for (auto& [net_idx, segment_set] : getGlobalNetResultMap(die)) {
      for (Segment<LayerCoord>* segment : segment_set) {
        RTDM.updateGlobalNetResultToGCellMap(ChangeType::kDel, net_idx, segment);
      }
    }
    for (auto& [net_idx, segment_set] : getDetailedNetResultMap(die)) {
      for (Segment<LayerCoord>* segment : segment_set) {
        RTDM.updateDetailedNetResultToGCellMap(ChangeType::kDel, net_idx, segment);
      }
    }
    for (auto& [net_idx, patch_set] : getNetPatchMap(die)) {
      for (EXTLayerRect* patch : patch_set) {
        RTDM.updateNetPatchToGCellMap(ChangeType::kDel, net_idx, patch);
      }
    }
    for (Violation* violation : getViolationSet(die)) {
      RTDM.updateViolationToGCellMap(ChangeType::kDel, violation);
    }
  }
  DataManager& operator=(const DataManager& other) = delete;
  DataManager& operator=(DataManager&& other) = delete;

#if 1  // build
  void buildConfig();
  void buildDatabase();
  void buildGCellAxis();
  void makeGCellAxis();
  int32_t getRecommendedPitch();
  std::vector<ScaleGrid> makeGCellGridList(Direction direction, int32_t recommended_pitch);
  std::vector<ScaleGrid> makeGCellGridList(std::vector<int32_t>& gcell_scale_list);
  void checkGCellAxis();
  void buildDie();
  void makeDie();
  void checkDie();
  void buildLayerList();
  void transLayerList();
  void makeLayerList();
  void checkLayerList();
  void buildLayerInfo();
  void buildLayerViaMasterList();
  void transLayerViaMasterList();
  void makeLayerViaMasterList();
  bool sortByMultiLevel(ViaMaster& via_master1, ViaMaster& via_master2);
  SortStatus sortByLayerDirectionPriority(ViaMaster& via_master1, ViaMaster& via_master2);
  SortStatus sortByWidthASC(ViaMaster& via_master1, ViaMaster& via_master2);
  SortStatus sortByLengthASC(ViaMaster& via_master1, ViaMaster& via_master2);
  SortStatus sortBySymmetryPriority(ViaMaster& via_master1, ViaMaster& via_master2);
  void buildLayerViaMasterInfo();
  void buildObstacleList();
  void transObstacleList();
  void makeObstacleList();
  void checkObstacleList();
  void buildNetList();
  void buildPinList(Net& net);
  void transPinList(Net& net);
  void makePinList(Net& net);
  void checkPinList(Net& net);
  void buildGCellMap();
  int32_t getIntervalIdx(int32_t scale_start, int32_t scale_end, int32_t interval_start, int32_t interval_end, int32_t interval_length);
  void buildDetectionDistance();
  void printConfig();
  void printDatabase();
  void writePYScript();
#endif
};

}  // namespace irt
