#include <gtest/gtest.h>

#include "bbs_mod_log.h"

using namespace bbs;

TEST(BbsModLog, EmptyInitially) {
  ModLog log;
  EXPECT_EQ(log.count(), 0u);
}

TEST(BbsModLog, RecordsInOrder) {
  ModLog log;
  log.record(1, ModAction::BAN, 2, 0xFF, 1000);
  log.record(1, ModAction::UNBAN, 2, 0xFF, 2000);

  ASSERT_EQ(log.count(), 2u);
  EXPECT_EQ(log.at(0).action, ModAction::BAN);
  EXPECT_EQ(log.at(0).timestamp, 1000u);
  EXPECT_EQ(log.at(1).action, ModAction::UNBAN);
  EXPECT_EQ(log.at(1).timestamp, 2000u);
}

TEST(BbsModLog, StoresActorTargetAndRoom) {
  ModLog log;
  log.record(5, ModAction::CLOSE_ROOM, kInvalidUserId, 2, 1000);
  const ModLogEntry& e = log.at(0);
  EXPECT_EQ(e.actor_id, 5u);
  EXPECT_EQ(e.target_id, kInvalidUserId);
  EXPECT_EQ(e.room_id, 2);
}

TEST(BbsModLog, OverwritesOldestWhenFull) {
  ModLog log;
  // Riempie oltre la capacita': la entry piu' vecchia (timestamp 0) deve sparire.
  for (uint32_t i = 0; i < 40; i++) {
    log.record(1, ModAction::BAN, 2, 0xFF, i);
  }
  // Capacita' nota (32): dopo 40 inserimenti restano le ultime 32 (timestamp 8..39).
  EXPECT_EQ(log.count(), 32u);
  EXPECT_EQ(log.at(0).timestamp, 8u);
  EXPECT_EQ(log.at(31).timestamp, 39u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
