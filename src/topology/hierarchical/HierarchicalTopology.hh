/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#ifndef __HIERARCHICALTOPOLOGY_HH__
#define __HIERARCHICALTOPOLOGY_HH__

#include "../Topology.hh"
#include "../TopologyConfig.hh"
#include "HierarchicalTopologyConfig.hh"

namespace Analytical {
class HierarchicalTopology : public Topology {
 public:
  using TopologyList = HierarchicalTopologyConfig::TopologyList;

  HierarchicalTopology(
      TopologyConfigs configs,
      HierarchicalTopologyConfig hierarchy_config,
      CostModel& cost_model) noexcept;

  ~HierarchicalTopology() noexcept override;

  double send(NpuId src, NpuId dest, PayloadSize payload_size) noexcept
      override;

 private:
  HierarchicalTopologyConfig hierarchy_config;

  Latency linkLatency(int dimension, int hops_count) const noexcept;

  NpuAddress npuIdToAddress(NpuId npu_id) const noexcept override;
  NpuId npuAddressToId(NpuAddress npu_address) const noexcept override;
};
} // namespace Analytical

#endif