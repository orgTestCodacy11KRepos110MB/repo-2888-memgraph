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

namespace memgraph::utils {

class AsyncTimer {
 public:
  explicit AsyncTimer(double seconds);
  AsyncTimer() = default;
  ~AsyncTimer() = default;
  AsyncTimer(AsyncTimer &&other) = default;
  AsyncTimer &operator=(AsyncTimer &&other) = default;
  AsyncTimer(const AsyncTimer &) = delete;
  AsyncTimer &operator=(const AsyncTimer &) = delete;

  // Returns false if the object isn't associated with any timer.
  bool IsExpired() const noexcept;
};

}
