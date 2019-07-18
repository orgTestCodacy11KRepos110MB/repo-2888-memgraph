#include "storage/distributed/vertex_accessor.hpp"

#include <algorithm>

#include "database/distributed/graph_db_accessor.hpp"
#include "durability/distributed/state_delta.hpp"
#include "utils/algorithm.hpp"

VertexAccessor::VertexAccessor(VertexAddress address,
                               database::GraphDbAccessor &db_accessor)
    : RecordAccessor(address, db_accessor) {
  Reconstruct();
}

size_t VertexAccessor::out_degree() const {
  auto guard = storage::GetDataLock(*this);
  return GetCurrent()->out_.size();
}

size_t VertexAccessor::in_degree() const {
  auto guard = storage::GetDataLock(*this);
  return GetCurrent()->in_.size();
}

void VertexAccessor::add_label(storage::Label label) {
  auto &dba = db_accessor();
  auto delta = database::StateDelta::AddLabel(dba.transaction_id(), gid(),
                                              label, dba.LabelName(label));
  auto guard = storage::GetDataLock(*this);
  update();
  auto &vertex = *GetNew();
  // not a duplicate label, add it
  if (!utils::Contains(vertex.labels_, label)) {
    vertex.labels_.emplace_back(label);
    if (is_local()) {
      dba.UpdateLabelIndices(label, *this, &vertex);
    }
    ProcessDelta(delta);
  }
}

void VertexAccessor::remove_label(storage::Label label) {
  auto &dba = db_accessor();
  auto delta = database::StateDelta::RemoveLabel(dba.transaction_id(), gid(),
                                                 label, dba.LabelName(label));
  auto guard = storage::GetDataLock(*this);
  update();
  auto &vertex = *GetNew();
  if (utils::Contains(vertex.labels_, label)) {
    auto &labels = vertex.labels_;
    auto found = std::find(labels.begin(), labels.end(), delta.label);
    std::swap(*found, labels.back());
    labels.pop_back();
    ProcessDelta(delta);
  }
}

bool VertexAccessor::has_label(storage::Label label) const {
  auto guard = storage::GetDataLock(*this);
  auto &labels = GetCurrent()->labels_;
  return std::find(labels.begin(), labels.end(), label) != labels.end();
}

std::vector<storage::Label> VertexAccessor::labels() const {
  auto guard = storage::GetDataLock(*this);
  return GetCurrent()->labels_;
}

void VertexAccessor::RemoveOutEdge(storage::EdgeAddress edge) {
  auto &dba = db_accessor();
  auto delta = database::StateDelta::RemoveOutEdge(
      dba.transaction_id(), gid(), dba.db().storage().GlobalizedAddress(edge));

  SwitchNew();
  auto guard = storage::GetDataLock(*this);
  if (GetCurrent()->is_expired_by(dba.transaction())) return;

  update();
  GetNew()->out_.RemoveEdge(
      dba.db().storage().LocalizedAddressIfPossible(edge));
  ProcessDelta(delta);
}

void VertexAccessor::RemoveInEdge(storage::EdgeAddress edge) {
  auto &dba = db_accessor();
  auto delta = database::StateDelta::RemoveInEdge(
      dba.transaction_id(), gid(), dba.db().storage().GlobalizedAddress(edge));

  SwitchNew();
  auto guard = storage::GetDataLock(*this);
  if (GetCurrent()->is_expired_by(dba.transaction())) return;

  update();
  GetNew()->in_.RemoveEdge(dba.db().storage().LocalizedAddressIfPossible(edge));
  ProcessDelta(delta);
}

std::ostream &operator<<(std::ostream &os, const VertexAccessor &va) {
  os << "V(";
  utils::PrintIterable(os, va.labels(), ":", [&](auto &stream, auto label) {
    stream << va.db_accessor().LabelName(label);
  });
  os << " {";
  utils::PrintIterable(os, va.Properties(), ", ",
                       [&](auto &stream, const auto &pair) {
                         stream << va.db_accessor().PropertyName(pair.first)
                                << ": " << pair.second;
                       });
  return os << "})";
}