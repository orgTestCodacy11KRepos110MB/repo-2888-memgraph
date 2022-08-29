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

#include <limits>
#include <tuple>
#include <type_traits>
#include <vector>

#include "storage/v3/delta.hpp"
#include "storage/v3/edge_ref.hpp"
#include "storage/v3/id_types.hpp"
#include "storage/v3/key_store.hpp"
#include "storage/v3/property_store.hpp"
#include "storage/v3/property_value.hpp"
#include "utils/algorithm.hpp"
#include "utils/spin_lock.hpp"

namespace memgraph::storage::v3 {

struct Vertex {
  Vertex(Delta *delta, LabelId primary_label, const std::vector<PropertyValue> &primary_properties)
      : primary_label{primary_label}, keys{primary_properties}, delta{delta} {
    MG_ASSERT(delta == nullptr || delta->action == Delta::Action::DELETE_OBJECT,
              "Vertex must be created with an initial DELETE_OBJECT delta!");
  }

  Vertex(Delta *delta, LabelId primary_label, const std::vector<PropertyValue> &primary_properties,
         const std::vector<LabelId> &secondary_labels,
         const std::vector<std::pair<PropertyId, PropertyValue>> &secondary_properties)
      : primary_label{primary_label}, keys{primary_properties}, labels{secondary_labels}, delta{delta} {
    MG_ASSERT(delta == nullptr || delta->action == Delta::Action::DELETE_OBJECT,
              "Vertex must be created with an initial DELETE_OBJECT delta!");
    for (const auto &[property_id, property_value] : secondary_properties) {
      properties.SetProperty(property_id, property_value);
    }
  }

  Vertex(LabelId primary_label, const std::vector<PropertyValue> &primary_properties)
      : primary_label{primary_label}, keys(primary_properties) {
    MG_ASSERT(delta == nullptr || delta->action == Delta::Action::DELETE_OBJECT,
              "Vertex must be created with an initial DELETE_OBJECT delta!");
  }
  Vertex(LabelId primary_label, const std::vector<PropertyValue> &primary_properties,
         const std::vector<LabelId> &secondary_labels,
         const std::vector<std::pair<PropertyId, PropertyValue>> &secondary_properties)
      : primary_label{primary_label}, keys{primary_properties}, labels{secondary_labels} {
    MG_ASSERT(delta == nullptr || delta->action == Delta::Action::DELETE_OBJECT,
              "Vertex must be created with an initial DELETE_OBJECT delta!");
    for (const auto &[property_id, property_value] : secondary_properties) {
      properties.SetProperty(property_id, property_value);
    }
  }

  LabelId primary_label;
  KeyStore keys;

  std::vector<LabelId> labels;
  PropertyStore properties;
  std::vector<std::tuple<EdgeTypeId, Vertex *, EdgeRef>> in_edges;
  std::vector<std::tuple<EdgeTypeId, Vertex *, EdgeRef>> out_edges;

  mutable utils::SpinLock lock;
  bool deleted{false};
  // uint8_t PAD;
  // uint16_t PAD;

  Delta *delta;
};

static_assert(alignof(Vertex) >= 8, "The Vertex should be aligned to at least 8!");

inline bool VertexHasLabel(const Vertex &vertex, const LabelId label) {
  return vertex.primary_label == label || utils::Contains(vertex.labels, label);
}

}  // namespace memgraph::storage::v3