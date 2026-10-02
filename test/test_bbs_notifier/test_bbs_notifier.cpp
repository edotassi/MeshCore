#include <gtest/gtest.h>

#include <string>

#include "bbs_fake_filesystem.h"
#include "bbs_fake_reply_channel.h"
#include "bbs_notifier.h"

using namespace bbs;
using bbs_test::FakeFileSystem;
using bbs_test::FakeReplyChannel;

namespace {

void makeKey(uint8_t out[BBS_PUBKEY_LEN], uint8_t seed) {
  for (size_t i = 0; i < BBS_PUBKEY_LEN; i++) out[i] = (uint8_t)(seed + i);
}

struct Fixture {
  FakeFileSystem fs;
  UserStore users{fs};
  SessionTable sessions;
  RoomRegistry room_registry{fs};
  FakeReplyChannel reply;
  Notifier notifier{users, sessions, room_registry, reply};

  UserId registerAndLogin(uint8_t seed, const char* nick, uint32_t now_ts) {
    uint8_t key[BBS_PUBKEY_LEN];
    makeKey(key, seed);
    UserId id;
    users.registerUser(key, nick, now_ts, id);
    sessions.touch(id, now_ts);
    return id;
  }
};

} // namespace

TEST(BbsNotifier, NoNotificationBeforeWindowElapses) {
  Fixture f;
  UserId author = f.registerAndLogin(1, "autore", 1000);
  UserId reader = f.registerAndLogin(2, "lettore", 1000);  // iscritto a 0 di default

  f.notifier.onNewPost(0, author, 1000);
  f.notifier.tick(1000 + BBS_NOTIFY_BATCH_WINDOW_SECS - 1);

  EXPECT_TRUE(f.reply.sent.empty());
}

TEST(BbsNotifier, SendsNotificationAfterWindowElapses) {
  Fixture f;
  UserId author = f.registerAndLogin(3, "autore2", 1000);
  UserId reader = f.registerAndLogin(4, "lettore2", 1000);

  f.notifier.onNewPost(0, author, 1000);
  f.notifier.tick(1000 + BBS_NOTIFY_BATCH_WINDOW_SECS);

  ASSERT_EQ(f.reply.sent.size(), 1u);
  EXPECT_EQ(f.reply.sent[0].text, "1 nuovi in Generale, N per leggere");
}

TEST(BbsNotifier, AccumulatesMultiplePostsIntoOneNotification) {
  Fixture f;
  UserId author = f.registerAndLogin(5, "autore3", 1000);
  UserId reader = f.registerAndLogin(6, "lettore3", 1000);

  f.notifier.onNewPost(0, author, 1000);
  f.notifier.onNewPost(0, author, 1010);
  f.notifier.onNewPost(0, author, 1020);
  f.notifier.tick(1000 + BBS_NOTIFY_BATCH_WINDOW_SECS);

  ASSERT_EQ(f.reply.sent.size(), 1u);
  EXPECT_EQ(f.reply.sent[0].text, "3 nuovi in Generale, N per leggere");
}

TEST(BbsNotifier, AuthorNeverNotifiedOfOwnPost) {
  Fixture f;
  UserId author = f.registerAndLogin(7, "solo", 1000);  // nessun altro iscritto

  f.notifier.onNewPost(0, author, 1000);
  f.notifier.tick(1000 + BBS_NOTIFY_BATCH_WINDOW_SECS);

  EXPECT_TRUE(f.reply.sent.empty());
}

TEST(BbsNotifier, UnsubscribedUserNotNotified) {
  Fixture f;
  UserId author = f.registerAndLogin(8, "autore4", 1000);
  UserId reader = f.registerAndLogin(9, "lettore4", 1000);
  f.users.setSubscribed(reader, 0, false);

  f.notifier.onNewPost(0, author, 1000);
  f.notifier.tick(1000 + BBS_NOTIFY_BATCH_WINDOW_SECS);

  EXPECT_TRUE(f.reply.sent.empty());
}

TEST(BbsNotifier, StopsNotifyingAfterTooManyMissedDeliveries) {
  Fixture f;
  UserId author = f.registerAndLogin(10, "autore5", 1000);
  UserId reader = f.registerAndLogin(11, "lettore5", 1000);

  for (uint8_t i = 0; i < BBS_MAX_MISSED_DELIVERIES; i++) f.sessions.recordDeliveryFailure(reader);
  ASSERT_FALSE(f.sessions.isReachable(reader));

  f.notifier.onNewPost(0, author, 1000);  // onNewPost salta chi non e' raggiungibile
  f.notifier.tick(1000 + BBS_NOTIFY_BATCH_WINDOW_SECS);

  EXPECT_TRUE(f.reply.sent.empty());
}

TEST(BbsNotifier, FailedDeliveryIsCountedAsMissed) {
  Fixture f;
  UserId author = f.registerAndLogin(12, "autore6", 1000);
  UserId reader = f.registerAndLogin(13, "lettore6", 1000);

  f.reply.fail_next = true;
  f.notifier.onNewPost(0, author, 1000);
  f.notifier.tick(1000 + BBS_NOTIFY_BATCH_WINDOW_SECS);

  EXPECT_TRUE(f.reply.sent.empty());  // l'invio e' fallito
  EXPECT_TRUE(f.sessions.isReachable(reader));  // un solo fallimento non basta a metterlo offline
}

TEST(BbsNotifier, BannedUserNeverNotified) {
  Fixture f;
  UserId author = f.registerAndLogin(14, "autore7", 1000);
  UserId reader = f.registerAndLogin(15, "lettore7", 1000);
  ASSERT_TRUE(f.users.setBanned(reader, true));

  f.notifier.onNewPost(0, author, 1000);
  f.notifier.tick(1000 + BBS_NOTIFY_BATCH_WINDOW_SECS);

  EXPECT_TRUE(f.reply.sent.empty());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
