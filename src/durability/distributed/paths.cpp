#include "durability/distributed/paths.hpp"

#include <filesystem>
#include <optional>
#include <string>

#include "glog/logging.h"

#include "transactions/type.hpp"
#include "utils/string.hpp"
#include "utils/timestamp.hpp"

namespace durability {

namespace fs = std::filesystem;

std::optional<tx::TransactionId> TransactionIdFromWalFilename(
    const std::string &name) {
  auto nullopt = std::nullopt;
  // Get the max_transaction_id from the file name that has format
  // "XXXXX__max_transaction_<MAX_TRANS_ID>_worker_<Worker_ID>"
  auto file_name_split = utils::RSplit(name, "__", 1);
  if (file_name_split.size() != 2) {
    LOG(WARNING) << "Unable to parse WAL file name: " << name;
    return nullopt;
  }
  if (utils::StartsWith(file_name_split[1], "current"))
    return std::numeric_limits<tx::TransactionId>::max();
  file_name_split = utils::Split(file_name_split[1], "_");
  if (file_name_split.size() != 5) {
    LOG(WARNING) << "Unable to parse WAL file name: " << name;
    return nullopt;
  }
  auto &tx_id_str = file_name_split[2];
  try {
    return std::stoll(tx_id_str);
  } catch (std::invalid_argument &) {
    LOG(WARNING) << "Unable to parse WAL file name tx ID: " << tx_id_str;
    return nullopt;
  } catch (std::out_of_range &) {
    LOG(WARNING) << "WAL file name tx ID too large: " << tx_id_str;
    return nullopt;
  }
}

fs::path MakeSnapshotPath(const fs::path &durability_dir, const int worker_id,
                          tx::TransactionId tx_id) {
  std::string date_str =
      utils::Timestamp(utils::Timestamp::Now())
          .ToString("{:04d}_{:02d}_{:02d}__{:02d}_{:02d}_{:02d}_{:05d}");
  auto file_name = date_str + "_worker_" + std::to_string(worker_id) + "_tx_" +
                   std::to_string(tx_id);
  return durability_dir / kSnapshotDir / file_name;
}

/// Generates a file path for a write-ahead log file. If given a transaction ID
/// the file name will contain it. Otherwise the file path is for the "current"
/// WAL file for which the max tx id is still unknown.
fs::path WalFilenameForTransactionId(const std::filesystem::path &wal_dir,
                                     int worker_id,
                                     std::optional<tx::TransactionId> tx_id) {
  auto file_name = utils::Timestamp::Now().ToIso8601();
  if (tx_id) {
    file_name += "__max_transaction_" + std::to_string(*tx_id);
  } else {
    file_name += "__current";
  }
  file_name = file_name + "_Worker_" + std::to_string(worker_id);
  return wal_dir / file_name;
}

std::optional<tx::TransactionId> TransactionIdFromSnapshotFilename(
    const std::string &name) {
  auto nullopt = std::nullopt;
  auto file_name_split = utils::RSplit(name, "_tx_", 1);
  if (file_name_split.size() != 2) {
    LOG(WARNING) << "Unable to parse snapshot file name: " << name;
    return nullopt;
  }
  try {
    return std::stoll(file_name_split[1]);
  } catch (std::invalid_argument &) {
    LOG(WARNING) << "Unable to parse snapshot file name tx ID: "
                 << file_name_split[1];
    return nullopt;
  } catch (std::out_of_range &) {
    LOG(WARNING) << "Unable to parse snapshot file name tx ID: "
                 << file_name_split[1];
    return nullopt;
  }
}
}  // namespace durability