#include <fstream>
#include <thread>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "utils/fswatcher.hpp"
#include "utils/signals/handler.hpp"
#include "utils/terminate_handler.hpp"

using namespace std::chrono_literals;
using namespace utils;

// TODO: This test is flaky, we should fix it sometime.

fs::path working_dir = "../data";
fs::path filename = "test.txt";
fs::path test_path = working_dir / filename;

void create_delete_loop(int iterations, ms action_delta) {
  for (int i = 0; i < iterations; ++i) {
    // create test file
    std::ofstream outfile(test_path);
    outfile.close();
    std::this_thread::sleep_for(action_delta);

    // remove test file
    fs::remove(test_path);
    std::this_thread::sleep_for(action_delta);
  }
}

void modify_loop(int iterations, ms action_delta) {
  // create test file
  std::ofstream outfile(test_path);
  outfile.close();
  std::this_thread::sleep_for(action_delta);

  // append TEST multiple times
  for (int i = 0; i < iterations; ++i) {
    outfile.open(test_path, std::ios_base::app);
    outfile << "TEST" << i;
    outfile.close();
    std::this_thread::sleep_for(action_delta);
  }

  // remove test file
  fs::remove(test_path);
  std::this_thread::sleep_for(action_delta);
}

TEST(FSWatcherTest, CreateDeleteLoop) {
  FSWatcher watcher;

  // parameters
  int iterations = 2;
  int created_no = 0;
  int deleted_no = 0;

  // 3 is here because the test has to ensure that there is enough time
  // between events in order to catch all relevant events (watcher takes a
  // sleep between two undelying calls and if there is no enough time
  // some events can be overriden)
  ms action_delta = watcher.check_interval() * 3;

  // watchers
  watcher.watch(WatchDescriptor(working_dir, FSEventType::Created),
                [&](FSEvent) {});
  watcher.watch(WatchDescriptor(working_dir, FSEventType::Deleted),
                [&](FSEvent) {});
  // above watchers should be ignored
  watcher.watch(WatchDescriptor(working_dir, FSEventType::All),
                [&](FSEvent event) {
                  if (event.type == FSEventType::Created) created_no++;
                  if (event.type == FSEventType::Deleted) deleted_no++;
                });

  ASSERT_EQ(watcher.size(), 1);

  create_delete_loop(iterations, action_delta);
  ASSERT_EQ(created_no, iterations);
  ASSERT_EQ(deleted_no, iterations);

  watcher.unwatchAll();
  ASSERT_EQ(watcher.size(), 0);

  watcher.unwatchAll();
  ASSERT_EQ(watcher.size(), 0);

  create_delete_loop(iterations, action_delta);
  ASSERT_EQ(created_no, iterations);
  ASSERT_EQ(deleted_no, iterations);
}

TEST(FSWatcherTest, ModifiyLoop) {
  FSWatcher watcher;

  // parameters
  int iterations = 2;
  int modified_no = 0;

  // 3 is here because the test has to ensure that there is enough time
  // between events in order to catch all relevant events (watcher takes a
  // sleep between two undelying calls and if there is no enough time
  // some events can be overriden)
  ms action_delta = watcher.check_interval() * 3;

  watcher.watch(WatchDescriptor(working_dir, FSEventType::Modified),
                [&](FSEvent) { modified_no++; });
  ASSERT_EQ(watcher.size(), 1);

  modify_loop(iterations, action_delta);
  ASSERT_EQ(modified_no, iterations);

  watcher.unwatch(WatchDescriptor(working_dir, FSEventType::Modified));
  ASSERT_EQ(watcher.size(), 0);

  watcher.unwatch(WatchDescriptor(working_dir, FSEventType::Modified));
  ASSERT_EQ(watcher.size(), 0);

  modify_loop(iterations, action_delta);
  ASSERT_EQ(modified_no, iterations);
}

int main(int argc, char **argv) {
  google::InitGoogleLogging(argv[0]);

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}