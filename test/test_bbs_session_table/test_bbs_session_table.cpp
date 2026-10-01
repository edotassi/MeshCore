#include <gtest/gtest.h>

#include "bbs_session_table.h"

using namespace bbs;

TEST(BbsSessionTable, NotActiveInitially) {
  SessionTable table;
  EXPECT_FALSE(table.isActive(1, 1000));
  EXPECT_EQ(table.count(), 0u);
}

TEST(BbsSessionTable, TouchThenActive) {
  SessionTable table;
  table.touch(1, 1000);
  EXPECT_TRUE(table.isActive(1, 1000));
  EXPECT_TRUE(table.isActive(1, 1000 + BBS_SESSION_TIMEOUT_SECS));  // al limite, ancora attiva
  EXPECT_EQ(table.count(), 1u);
}

TEST(BbsSessionTable, ExpiresAfterTimeout) {
  SessionTable table;
  table.touch(1, 1000);
  EXPECT_FALSE(table.isActive(1, 1000 + BBS_SESSION_TIMEOUT_SECS + 1));
}

TEST(BbsSessionTable, TouchAgainResetsActivity) {
  SessionTable table;
  table.touch(1, 1000);
  table.touch(1, 1000 + BBS_SESSION_TIMEOUT_SECS);  // rinnova prima della scadenza
  EXPECT_TRUE(table.isActive(1, 1000 + BBS_SESSION_TIMEOUT_SECS + BBS_SESSION_TIMEOUT_SECS));
}

TEST(BbsSessionTable, LogoutDeactivatesImmediately) {
  SessionTable table;
  table.touch(1, 1000);
  table.logout(1);
  EXPECT_FALSE(table.isActive(1, 1000));
  EXPECT_EQ(table.count(), 0u);
}

TEST(BbsSessionTable, MultipleUsersIndependent) {
  SessionTable table;
  table.touch(1, 1000);
  table.touch(2, 2000);
  EXPECT_TRUE(table.isActive(1, 1000));
  EXPECT_TRUE(table.isActive(2, 2000));
  table.logout(1);
  EXPECT_FALSE(table.isActive(1, 1000));
  EXPECT_TRUE(table.isActive(2, 2000));
  EXPECT_EQ(table.count(), 1u);
}

TEST(BbsSessionTable, EvictsLeastRecentlyActiveWhenFull) {
  SessionTable table;
  for (UserId i = 0; i < BBS_MAX_SESSIONS; i++) {
    table.touch(i, 1000 + i);  // user 0 e' il meno recentemente attivo
  }
  EXPECT_EQ(table.count(), BBS_MAX_SESSIONS);

  // Aggiunge un utente in piu': deve sostituire lo slot piu' vecchio (user 0).
  table.touch((UserId)BBS_MAX_SESSIONS, 1000 + BBS_MAX_SESSIONS);

  EXPECT_EQ(table.count(), BBS_MAX_SESSIONS);
  EXPECT_FALSE(table.isActive(0, 1000));
  EXPECT_TRUE(table.isActive((UserId)BBS_MAX_SESSIONS, 1000 + BBS_MAX_SESSIONS));
  EXPECT_TRUE(table.isActive(1, 1001));  // il resto e' rimasto
}

TEST(BbsSessionTable, NoPendingInitially) {
  SessionTable table;
  table.touch(1, 1000);
  EXPECT_FALSE(table.hasPendingReadyToFlush(1, 0, 1000 + BBS_NOTIFY_BATCH_WINDOW_SECS));
  EXPECT_EQ(table.consumePending(1, 0), 0u);
}

TEST(BbsSessionTable, PendingNotReadyBeforeWindowElapses) {
  SessionTable table;
  table.touch(1, 1000);
  table.addPendingPost(1, 0, 1000);
  EXPECT_FALSE(table.hasPendingReadyToFlush(1, 0, 1000 + BBS_NOTIFY_BATCH_WINDOW_SECS - 1));
  EXPECT_TRUE(table.hasPendingReadyToFlush(1, 0, 1000 + BBS_NOTIFY_BATCH_WINDOW_SECS));
}

TEST(BbsSessionTable, MultiplePostsAccumulateIntoOneCount) {
  SessionTable table;
  table.touch(1, 1000);
  table.addPendingPost(1, 0, 1000);
  table.addPendingPost(1, 0, 1010);
  table.addPendingPost(1, 0, 1020);
  // la finestra parte dal PRIMO post in sospeso, non dall'ultimo.
  EXPECT_FALSE(table.hasPendingReadyToFlush(1, 0, 1000 + BBS_NOTIFY_BATCH_WINDOW_SECS - 1));
  ASSERT_TRUE(table.hasPendingReadyToFlush(1, 0, 1000 + BBS_NOTIFY_BATCH_WINDOW_SECS));
  EXPECT_EQ(table.consumePending(1, 0), 3u);
  EXPECT_EQ(table.consumePending(1, 0), 0u);  // consumato, non si ripete
}

TEST(BbsSessionTable, PendingIsPerRoom) {
  SessionTable table;
  table.touch(1, 1000);
  table.addPendingPost(1, 0, 1000);
  EXPECT_EQ(table.consumePending(1, 1), 0u);
  EXPECT_EQ(table.consumePending(1, 0), 1u);
}

TEST(BbsSessionTable, ReachableByDefaultThenUnreachableAfterMissedDeliveries) {
  SessionTable table;
  table.touch(1, 1000);
  EXPECT_TRUE(table.isReachable(1));
  for (uint8_t i = 0; i < BBS_MAX_MISSED_DELIVERIES - 1; i++) {
    table.recordDeliveryFailure(1);
    EXPECT_TRUE(table.isReachable(1));
  }
  table.recordDeliveryFailure(1);
  EXPECT_FALSE(table.isReachable(1));
}

TEST(BbsSessionTable, TouchRestoresReachability) {
  SessionTable table;
  table.touch(1, 1000);
  for (uint8_t i = 0; i < BBS_MAX_MISSED_DELIVERIES; i++) table.recordDeliveryFailure(1);
  ASSERT_FALSE(table.isReachable(1));
  table.touch(1, 2000);  // l'utente ha ricontattato la BBS
  EXPECT_TRUE(table.isReachable(1));
}

TEST(BbsSessionTable, DeliverySuccessResetsMissedCounter) {
  SessionTable table;
  table.touch(1, 1000);
  table.recordDeliveryFailure(1);
  table.recordDeliveryFailure(1);
  table.recordDeliverySuccess(1);
  for (uint8_t i = 0; i < BBS_MAX_MISSED_DELIVERIES - 1; i++) table.recordDeliveryFailure(1);
  EXPECT_TRUE(table.isReachable(1));  // il contatore era stato azzerato dal successo
}

TEST(BbsSessionTable, AllowMessageUnderLimit) {
  SessionTable table;
  table.touch(1, 1000);
  for (uint8_t i = 0; i < BBS_MAX_MESSAGES_PER_MINUTE; i++) {
    EXPECT_TRUE(table.allowMessage(1, 1000));
  }
}

TEST(BbsSessionTable, BlocksMessageOverLimitWithinSameWindow) {
  SessionTable table;
  table.touch(1, 1000);
  for (uint8_t i = 0; i < BBS_MAX_MESSAGES_PER_MINUTE; i++) table.allowMessage(1, 1000);
  EXPECT_FALSE(table.allowMessage(1, 1000));
  EXPECT_FALSE(table.allowMessage(1, 1030));  // ancora nella stessa finestra di 60s
}

TEST(BbsSessionTable, AllowsAgainAfterWindowResets) {
  SessionTable table;
  table.touch(1, 1000);
  for (uint8_t i = 0; i < BBS_MAX_MESSAGES_PER_MINUTE; i++) table.allowMessage(1, 1000);
  ASSERT_FALSE(table.allowMessage(1, 1000));
  EXPECT_TRUE(table.allowMessage(1, 1060));  // nuova finestra
}

TEST(BbsSessionTable, RateLimitIsPerUser) {
  SessionTable table;
  table.touch(1, 1000);
  table.touch(2, 1000);
  for (uint8_t i = 0; i < BBS_MAX_MESSAGES_PER_MINUTE; i++) table.allowMessage(1, 1000);
  EXPECT_FALSE(table.allowMessage(1, 1000));
  EXPECT_TRUE(table.allowMessage(2, 1000));  // l'altro utente non e' limitato
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
