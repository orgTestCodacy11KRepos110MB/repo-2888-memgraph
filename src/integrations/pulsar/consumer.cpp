// Copyright 2021 Memgraph Ltd.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.txt; by using this file, you agree to be bound by the terms of the Business Source
// License, and you may not use this file except in compliance with the Business Source License.
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0, included in the file
// licenses/APL.txt.

#include "integrations/pulsar/consumer.hpp"

#include <fmt/format.h>
#include <pulsar/Client.h>

#include <chrono>
#include <thread>

#include "integrations/constants.hpp"
#include "integrations/pulsar/exceptions.hpp"
#include "utils/concepts.hpp"
#include "utils/logging.hpp"
#include "utils/on_scope_exit.hpp"
#include "utils/result.hpp"
#include "utils/thread.hpp"

namespace integrations::pulsar {

namespace {
utils::BasicResult<std::string, std::vector<Message>> GetBatch(pulsar_client::Consumer &consumer,
                                                               const ConsumerInfo &info, std::atomic<bool> &is_running,
                                                               uint64_t last_publish_time) {
  std::vector<Message> batch{};

  const auto batch_size = info.batch_size.value_or(kDefaultBatchSize);
  batch.reserve(batch_size);

  auto remaining_timeout_in_ms = info.batch_interval.value_or(kDefaultBatchInterval).count();
  auto start = std::chrono::steady_clock::now();

  bool run_batch = true;
  for (int64_t i = 0; remaining_timeout_in_ms > 0 && i < batch_size && is_running.load(); ++i) {
    pulsar_client::Message message;
    const auto result = consumer.receive(message, static_cast<int>(remaining_timeout_in_ms));
    switch (result) {
      case pulsar_client::Result::ResultTimeout:
        run_batch = false;
        break;
      case pulsar_client::Result::ResultOk:
        if (message.getPublishTimestamp() < last_publish_time) {
          // we have out of order messages (left over from previous receives)
          // and the message will be redelivered later on
          batch.clear();
        }

        last_publish_time = message.getPublishTimestamp();
        batch.emplace_back(Message{std::move(message)});
        break;
      default:
        spdlog::warn(fmt::format("Unexpected error while consuming message from consumer {}, error: {}",
                                 info.consumer_name, result));
        return {pulsar_client::strResult(result)};
    }

    if (!run_batch) {
      break;
    }

    auto now = std::chrono::steady_clock::now();
    auto took = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
    remaining_timeout_in_ms = remaining_timeout_in_ms - took.count();
    start = now;
  }

  return std::move(batch);
}
}  // namespace

Message::Message(pulsar_client::Message &&message) : message_{std::move(message)} {}

std::span<const char> Message::Payload() const {
  return {static_cast<const char *>(message_.getData()), message_.getLength()};
}

Consumer::Consumer(const std::string &cluster, ConsumerInfo info, ConsumerFunction consumer_function)
    : info_{std::move(info)}, client_{cluster}, consumer_function_{std::move(consumer_function)} {
  pulsar_client::ConsumerConfiguration config;
  config.setSubscriptionInitialPosition(pulsar_client::InitialPositionEarliest);
  config.setConsumerType(pulsar_client::ConsumerType::ConsumerExclusive);
  if (pulsar_client::Result result = client_.subscribe(info_.topics, info_.consumer_name, config, consumer_);
      result != pulsar_client::ResultOk) {
    throw ConsumerFailedToInitializeException(info_.consumer_name, pulsar_client::strResult(result));
  }
}
Consumer::~Consumer() {
  StopIfRunning();
  client_.close();
}

bool Consumer::IsRunning() const { return is_running_; }

const ConsumerInfo &Consumer::Info() const { return info_; }

void Consumer::Start() {
  if (is_running_) {
    throw ConsumerRunningException(info_.consumer_name);
  }

  StartConsuming();
}

void Consumer::Stop() {
  if (!is_running_) {
    throw ConsumerStoppedException(info_.consumer_name);
  }
  StopConsuming();
}

void Consumer::StopIfRunning() {
  if (is_running_) {
    StopConsuming();
  }

  if (thread_.joinable()) {
    thread_.join();
  }
}

void Consumer::Check(std::optional<std::chrono::milliseconds> timeout, std::optional<int64_t> limit_batches,
                     const ConsumerFunction &check_consumer_function) const {
  // NOLINTNEXTLINE (modernize-use-nullptr)
  if (timeout.value_or(kMinimumInterval) < kMinimumInterval) {
    throw ConsumerCheckFailedException(info_.consumer_name, "Timeout has to be positive!");
  }
  if (limit_batches.value_or(kMinimumSize) < kMinimumSize) {
    throw ConsumerCheckFailedException(info_.consumer_name, "Batch limit has to be positive!");
  }
  // The implementation of this function is questionable: it is const qualified, though it changes the inner state of
  // KafkaConsumer. Though it changes the inner state, it saves the current assignment for future Check/Start calls to
  // restore the current state, so the changes made by this function shouldn't be visible for the users of the class. It
  // also passes a non const reference of KafkaConsumer to GetBatch function. That means the object is bitwise const
  // (KafkaConsumer is stored in unique_ptr) and internally mostly synchronized. Mostly, because as Start/Stop requires
  // exclusive access to consumer, so we don't have to deal with simultaneous calls to those functions. The only concern
  // in this function is to prevent executing this function on multiple threads simultaneously.
  if (is_running_.exchange(true)) {
    throw ConsumerRunningException(info_.consumer_name);
  }

  utils::OnScopeExit restore_is_running([this] { is_running_.store(false); });

  const auto num_of_batches = limit_batches.value_or(kDefaultCheckBatchLimit);
  const auto timeout_to_use = timeout.value_or(kDefaultCheckTimeout);
  const auto start = std::chrono::steady_clock::now();

  consumer_.redeliverUnacknowledgedMessages();

  for (int64_t i = 0; i < num_of_batches;) {
    const auto now = std::chrono::steady_clock::now();
    // NOLINTNEXTLINE (modernize-use-nullptr)
    if (now - start >= timeout_to_use) {
      throw ConsumerCheckFailedException(info_.consumer_name, "Timeout reached");
    }

    auto maybe_batch = GetBatch(consumer_, info_, is_running_, last_publish_time_);

    if (maybe_batch.HasError()) {
      throw ConsumerCheckFailedException(info_.consumer_name, maybe_batch.GetError());
    }

    const auto &batch = maybe_batch.GetValue();

    if (batch.empty()) {
      continue;
    }
    ++i;

    try {
      check_consumer_function(batch);
    } catch (const std::exception &e) {
      spdlog::warn("Pulsar consumer {} check failed with error {}", info_.consumer_name, e.what());
      throw ConsumerCheckFailedException(info_.consumer_name, e.what());
    }
  }
}

void Consumer::StartConsuming() {
  MG_ASSERT(!is_running_, "Cannot start already running consumer!");
  if (thread_.joinable()) {
    thread_.join();
  }

  is_running_.store(true);

  thread_ = std::thread([this] {
    constexpr auto kMaxThreadNameSize = utils::GetMaxThreadNameSize();
    const auto full_thread_name = "Cons#" + info_.consumer_name;

    utils::ThreadSetName(full_thread_name.substr(0, kMaxThreadNameSize));

    consumer_.redeliverUnacknowledgedMessages();

    while (is_running_) {
      auto maybe_batch = GetBatch(consumer_, info_, is_running_, last_publish_time_);

      if (maybe_batch.HasError()) {
        spdlog::warn("Error happened in consumer {} while fetching messages: {}!", info_.consumer_name,
                     maybe_batch.GetError());
        break;
      }

      const auto &batch = maybe_batch.GetValue();

      if (batch.empty()) continue;

      spdlog::info("Pulsar consumer {} is processing a batch", info_.consumer_name);

      try {
        consumer_function_(batch);

        const auto &last_message = batch.back().message_;
        for (const auto &message : batch) {
          // acknowledgeCumulative doesn't work with multiple topics
          // we either pick support for mulitiple topics or an all or nothing acknowledgment
          if (const auto result = consumer_.acknowledge(message.message_); result != pulsar_client::ResultOk) {
            spdlog::warn("Acknowledging a message of consumer {} failed: {}", info_.consumer_name, result);
            break;
          }
          last_publish_time_ = last_message.getPublishTimestamp();
        }
      } catch (const std::exception &e) {
        spdlog::warn("Error happened in consumer {} while processing a batch: {}!", info_.consumer_name, e.what());
        break;
      }

      spdlog::info("Pulsar consumer {} finished processing", info_.consumer_name);
    }
    is_running_.store(false);
  });
}

void Consumer::StopConsuming() {
  is_running_.store(false);
  if (thread_.joinable()) {
    thread_.join();
  }
}

}  // namespace integrations::pulsar
