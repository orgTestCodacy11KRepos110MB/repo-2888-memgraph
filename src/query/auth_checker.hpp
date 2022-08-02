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

#include "auth/models.hpp"
#include "query/frontend/ast/ast.hpp"
namespace memgraph::query {
class AuthChecker {
 public:
  virtual bool IsUserAuthorized(const std::optional<std::string> &username,
                                const std::vector<query::AuthQuery::Privilege> &privileges) const = 0;

  virtual bool IsUserAuthorizedLabels(const memgraph::auth::User *user, const memgraph::query::DbAccessor *dba,
                                      const std::vector<memgraph::storage::LabelId> &labels) const = 0;

  virtual bool IsUserAuthorizedEdgeType(const memgraph::auth::User *user, const memgraph::query::DbAccessor *dba,
                                        const memgraph::storage::EdgeTypeId &edgeType) const = 0;
};

class AllowEverythingAuthChecker final : public query::AuthChecker {
  bool IsUserAuthorized(const std::optional<std::string> &username,
                        const std::vector<query::AuthQuery::Privilege> &privileges) const override {
    return true;
  }
  bool IsUserAuthorizedLabels(const memgraph::auth::User *user, const memgraph::query::DbAccessor *dba,
                              const std::vector<memgraph::storage::LabelId> &labels) const override {
    return true;
  };

  bool IsUserAuthorizedEdgeType(const memgraph::auth::User *user, const memgraph::query::DbAccessor *dba,
                                const memgraph::storage::EdgeTypeId &edgeType) const override {
    return true;
  };
};
}  // namespace memgraph::query
