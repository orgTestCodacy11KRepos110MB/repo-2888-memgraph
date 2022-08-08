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

#pragma once

#include <unordered_set>

#include "coordinator/hybrid_logical_clock.hpp"
#include "coordinator/shard_map.hpp"
#include "io/simulator/simulator.hpp"
#include "io/time.hpp"
#include "io/transport.hpp"

namespace memgraph::coordinator {

using Address = memgraph::io::Address;
using SimT = memgraph::io::simulator::SimulatorTransport;

struct HlcRequest {
  Hlc last_shard_map_version;
};

struct HlcResponse {
  Hlc new_hlc;
  std::optional<ShardMap> fresher_shard_map;
};

struct GetShardMapRequest {
  // No state
};

struct GetShardMapResponse {
  ShardMap shard_map;
};

struct AllocateHlcBatchRequest {
  Hlc low;
  Hlc high;
};

struct AllocateHlcBatchResponse {
  bool success;
  Hlc low;
  Hlc high;
};

struct SplitShardRequest {
  Hlc previous_shard_map_version;
  Label label;
  CompoundKey split_key;
};

struct SplitShardResponse {
  bool success;
};

struct RegisterStorageEngineRequest {
  Address address;
};

struct RegisterStorageEngineResponse {
  bool success;
};

struct DeregisterStorageEngineRequest {
  Address address;
};

struct DeregisterStorageEngineResponse {
  bool success;
};

using WriteRequests = std::variant<AllocateHlcBatchRequest, SplitShardRequest, RegisterStorageEngineRequest,
                                   DeregisterStorageEngineRequest>;
using WriteResponses = std::variant<AllocateHlcBatchResponse, SplitShardResponse, RegisterStorageEngineResponse,
                                    DeregisterStorageEngineResponse>;

using ReadRequests = std::variant<HlcRequest, GetShardMapRequest>;
using ReadResponses = std::variant<HlcResponse, GetShardMapResponse>;

class Coordinator {
  using StandbySotrageEnginePool = std::unordered_set<Address>;

  ShardMap shard_map_;
  // StandbySotrageEnginePool storage_engine_pool_;

  /// The highest reserved timestamp / highest allocated timestamp
  /// is a way for minimizing communication involved in query engines
  /// reserving Hlc's for their transaction processing.
  /// Periodically, the coordinator will allocate a batch of timestamps
  /// and this will need to go over consensus. From that point forward,
  /// each timestamp in that batch can be given out to "readers" who issue
  /// HlcRequest without blocking on consensus first. But if
  /// highest_allocated_timestamp_ approaches highest_reserved_timestamp_,
  /// it is time to allocate another batch, so that we can keep guaranteeing
  /// forward progress.
  /// Any time a coordinator becomes a new leader, it will need to issue
  /// a new AllocateHlcBatchRequest to create a pool of IDs to allocate.
  uint64_t highest_allocated_timestamp_;
  uint64_t highest_reserved_timestamp_;

  /// Increment our
  ReadResponses Read(HlcRequest &&hlc_request) {
    HlcResponse res{};

    return res;
  }

  GetShardMapResponse Read(GetShardMapRequest &&get_shard_map_request) {
    GetShardMapResponse res;
    res.shard_map = shard_map_;

    return res;
  }

  WriteResponses Apply(AllocateHlcBatchRequest &&ahr) {
    AllocateHlcBatchResponse res{};

    return res;
  }

  /// This splits the shard immediately beneath the provided
  /// split key, keeping the assigned peers identical for now,
  /// but letting them be gradually migrated over time.
  WriteResponses Apply(SplitShardRequest &&split_shard_request) {
    SplitShardResponse res{};

    if (split_shard_request.previous_shard_map_version != shard_map_.shard_map_version) {
      // TODO reply with failure
      res.success = false;
    } else {
      // TODO apply split
    }

    return res;
  }

  /// This adds the provided storage engine to the standby storage engine pool,
  /// which can be used to rebalance storage over time.
  WriteResponses Apply(RegisterStorageEngineRequest &&register_storage_engine_request) {
    RegisterStorageEngineResponse res{};
    // TODO
    // const Address &address = register_storage_engine_request.address;
    // storage_engine_pool_.insert(address);
    // res.success = true;

    return res;
  }

  /// This begins the process of draining the provided storage engine from all raft
  /// clusters that it might be participating in.
  WriteResponses Apply(DeregisterStorageEngineRequest &&register_storage_engine_request) {
    DeregisterStorageEngineResponse res{};
    // TODO
    // const Address &address = register_storage_engine_request.address;
    // storage_engine_pool_.erase(address);
    // res.success = true;

    return res;
  }

 public:
  ReadResponses Read(ReadRequests requests) {
    return std::visit([&](auto &&requests) { return Read(requests); }, std::move(requests));
  }

  WriteResponses Apply(WriteRequests requests) {
    return std::visit([&](auto &&requests) { return Apply(requests); }, std::move(requests));
  }
};

}  // namespace memgraph::coordinator
