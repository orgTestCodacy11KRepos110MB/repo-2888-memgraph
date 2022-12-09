// Copyright 2022 Memgraph Ltd.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.txt; by using this file, you agree to be bound by the terms of the Business Source
// License, and you may not use this file except in compliance with the Business Source License.
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0, included in the file
// licenses/APL.txt.

#include <chrono>
#include <limits>
#include <thread>

#include <gtest/gtest.h>

#include <coordinator/coordinator.hpp>
#include <coordinator/coordinator_client.hpp>
#include <coordinator/hybrid_logical_clock.hpp>
#include <coordinator/shard_map.hpp>
#include <io/local_transport/local_system.hpp>
#include <io/local_transport/local_transport.hpp>
#include <io/rsm/rsm_client.hpp>
#include <io/transport.hpp>
#include <machine_manager/machine_config.hpp>
#include <machine_manager/machine_manager.hpp>
#include <query/v2/requests.hpp>
#include "io/rsm/rsm_client.hpp"
#include "query/v2/request_router.hpp"
#include "storage/v3/id_types.hpp"
#include "storage/v3/schemas.hpp"

namespace memgraph::io::tests {

static const std::string kLabelName{"test_label"};
static const std::string kProperty1{"property_1"};
static const std::string kProperty2{"property_2"};

using memgraph::coordinator::Coordinator;
using memgraph::coordinator::CoordinatorClient;
using memgraph::coordinator::CoordinatorReadRequests;
using memgraph::coordinator::CoordinatorReadResponses;
using memgraph::coordinator::CoordinatorWriteRequests;
using memgraph::coordinator::CoordinatorWriteResponses;
using memgraph::coordinator::Hlc;
using memgraph::coordinator::HlcResponse;
using memgraph::coordinator::Shard;
using memgraph::coordinator::ShardMap;
using memgraph::io::Io;
using memgraph::io::local_transport::LocalSystem;
using memgraph::io::local_transport::LocalTransport;
using memgraph::machine_manager::MachineConfig;
using memgraph::machine_manager::MachineManager;
using memgraph::storage::v3::LabelId;
using memgraph::storage::v3::SchemaProperty;

using memgraph::io::rsm::RsmClient;
using memgraph::msgs::ReadRequests;
using memgraph::msgs::ReadResponses;
using memgraph::msgs::WriteRequests;
using memgraph::msgs::WriteResponses;

using CompoundKey = std::vector<memgraph::storage::v3::PropertyValue>;
using ShardClient = RsmClient<LocalTransport, WriteRequests, WriteResponses, ReadRequests, ReadResponses>;

ShardMap TestShardMap() {
  ShardMap sm{};

  // register new properties
  const std::vector<std::string> property_names = {kProperty1, kProperty2};
  const auto properties = sm.AllocatePropertyIds(property_names);
  const auto property_id_1 = properties.at(kProperty1);
  const auto property_id_2 = properties.at(kProperty2);
  const auto type_1 = memgraph::common::SchemaType::INT;
  const auto type_2 = memgraph::common::SchemaType::INT;

  // register new label space
  std::vector<SchemaProperty> schema = {
      SchemaProperty{.property_id = property_id_1, .type = type_1},
      SchemaProperty{.property_id = property_id_2, .type = type_2},
  };

  const size_t replication_factor = 1;

  const auto label_id = sm.InitializeNewLabel(kLabelName, schema, replication_factor, sm.shard_map_version);
  EXPECT_TRUE(label_id.has_value());

  sm.AllocateEdgeTypeIds(std::vector<std::string>{"edge_type"});
  // split the shard at N split points
  // NB: this is the logic that should be provided by the "split file"
  // TODO(tyler) split points should account for signedness
  const size_t n_splits = 16;
  const auto split_interval = std::numeric_limits<int64_t>::max() / n_splits;

  for (int64_t i = 0; i < n_splits; ++i) {
    const int64_t value = i * split_interval;

    const auto key1 = memgraph::storage::v3::PropertyValue(value);
    const auto key2 = memgraph::storage::v3::PropertyValue(0);

    const CompoundKey split_point = {key1, key2};

    const auto split_success = sm.SplitShard(sm.shard_map_version, label_id.value(), split_point);

    EXPECT_TRUE(split_success);
  }

  return sm;
}

template <typename RequestRouter>
void TestScanAll(RequestRouter &request_router) {
  auto result = request_router.ScanVertices(kLabelName);
  EXPECT_EQ(result.size(), 2);
}

void TestCreateVertices(query::v2::RequestRouterInterface &request_router) {
  using PropVal = msgs::Value;
  std::vector<msgs::NewVertex> new_vertices;
  auto label_id = request_router.NameToLabel(kLabelName);
  msgs::NewVertex a1{.primary_key = {PropVal(int64_t(0)), PropVal(int64_t(0))}};
  a1.label_ids.push_back({label_id});
  msgs::NewVertex a2{.primary_key = {PropVal(int64_t(13)), PropVal(int64_t(13))}};
  a2.label_ids.push_back({label_id});
  new_vertices.push_back(std::move(a1));
  new_vertices.push_back(std::move(a2));

  auto result = request_router.CreateVertices(std::move(new_vertices));
  EXPECT_EQ(result.size(), 1);
  EXPECT_FALSE(result[0].error.has_value()) << result[0].error->message;
}

void TestCreateExpand(query::v2::RequestRouterInterface &request_router) {
  using PropVal = msgs::Value;
  std::vector<msgs::NewExpand> new_expands;

  const auto edge_type_id = request_router.NameToEdgeType("edge_type");
  const auto label = msgs::Label{request_router.NameToLabel("test_label")};
  const msgs::VertexId vertex_id_1{label, {PropVal(int64_t(0)), PropVal(int64_t(0))}};
  const msgs::VertexId vertex_id_2{label, {PropVal(int64_t(13)), PropVal(int64_t(13))}};
  msgs::NewExpand expand_1{
      .id = {.gid = 0}, .type = {edge_type_id}, .src_vertex = vertex_id_1, .dest_vertex = vertex_id_2};
  msgs::NewExpand expand_2{
      .id = {.gid = 1}, .type = {edge_type_id}, .src_vertex = vertex_id_2, .dest_vertex = vertex_id_1};
  new_expands.push_back(std::move(expand_1));
  new_expands.push_back(std::move(expand_2));

  auto responses = request_router.CreateExpand(std::move(new_expands));
  MG_ASSERT(responses.size() == 1);
  MG_ASSERT(!responses[0].error.has_value());
}

void TestExpandOne(query::v2::RequestRouterInterface &request_router) {
  msgs::ExpandOneRequest request;
  const auto edge_type_id = request_router.NameToEdgeType("edge_type");
  const auto label = msgs::Label{request_router.NameToLabel("test_label")};
  request.src_vertices.push_back(msgs::VertexId{label, {msgs::Value(int64_t(0)), msgs::Value(int64_t(0))}});
  request.edge_types.push_back(msgs::EdgeType{edge_type_id});
  request.direction = msgs::EdgeDirection::BOTH;
  auto result_rows = request_router.ExpandOne(std::move(request));
  MG_ASSERT(result_rows.size() == 1);
  MG_ASSERT(result_rows[0].in_edges_with_all_properties.size() == 1);
  MG_ASSERT(result_rows[0].out_edges_with_all_properties.size() == 1);
}

template <typename RequestRouter>
void TestAggregate(RequestRouter &request_router) {}

MachineManager<LocalTransport> MkMm(LocalSystem &local_system, std::vector<Address> coordinator_addresses, Address addr,
                                    ShardMap shard_map) {
  MachineConfig config{
      .coordinator_addresses = coordinator_addresses,
      .is_storage = true,
      .is_coordinator = true,
      .listen_ip = addr.last_known_ip,
      .listen_port = addr.last_known_port,
  };

  Io<LocalTransport> io = local_system.Register(addr);

  Coordinator coordinator{shard_map};

  return MachineManager{io, config, coordinator};
}

void RunMachine(MachineManager<LocalTransport> mm) { mm.Run(); }

void WaitForShardsToInitialize(CoordinatorClient<LocalTransport> &cc) {
  // TODO(tyler) call coordinator client's read method for GetShardMap
  // and keep reading it until the shard map contains proper replicas
  // for each shard in the label space.

  using namespace std::chrono_literals;
  std::this_thread::sleep_for(2010ms);
}

TEST(MachineManager, BasicFunctionality) {
  LocalSystem local_system;

  auto cli_addr = Address::TestAddress(1);
  auto machine_1_addr = cli_addr.ForkUniqueAddress();

  Io<LocalTransport> cli_io = local_system.Register(cli_addr);

  auto coordinator_addresses = std::vector{
      machine_1_addr,
  };

  ShardMap initialization_sm = TestShardMap();

  auto mm_1 = MkMm(local_system, coordinator_addresses, machine_1_addr, initialization_sm);
  Address coordinator_address = mm_1.CoordinatorAddress();

  auto mm_thread_1 = std::jthread(RunMachine, std::move(mm_1));

  // TODO(tyler) clarify addresses of coordinator etc... as it's a mess
  CoordinatorClient<LocalTransport> cc{cli_io, coordinator_address, {coordinator_address}};

  WaitForShardsToInitialize(cc);

  CoordinatorClient<LocalTransport> coordinator_client(cli_io, coordinator_address, {coordinator_address});

  query::v2::RequestRouter<LocalTransport> request_router(std::move(coordinator_client), std::move(cli_io));

  request_router.StartTransaction();
  TestCreateVertices(request_router);
  TestScanAll(request_router);
  TestCreateExpand(request_router);
  TestExpandOne(request_router);
  local_system.ShutDown();
};

}  // namespace memgraph::io::tests