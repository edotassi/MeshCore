#include <gtest/gtest.h>

#include <cstring>
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
  PostStore posts{fs};
  FakeReplyChannel reply;
  Notifier notifier{users, sessions, room_registry, posts, reply};

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

TEST(BbsNotifier, PushesNewPostOnTick) {
  Fixture f;
  UserId author = f.registerAndLogin(1, "autore", 1000);  // entrambi in Generale (0) di default
  UserId reader = f.registerAndLogin(2, "lettore", 1000);

  ASSERT_TRUE(f.posts.appendPost(0, author, 1000, "ciao a tutti"));
  f.notifier.tick(1001);

  ASSERT_EQ(f.reply.sent.size(), 1u);
  EXPECT_EQ(f.reply.sent[0].text, "autore: ciao a tutti");

  uint8_t author_key[BBS_PUBKEY_LEN];
  ASSERT_TRUE(f.users.getPubkey(author, author_key));
  EXPECT_EQ(memcmp(f.reply.sent[0].author_pub_key, author_key, BBS_PUBKEY_LEN), 0);

  uint8_t reader_key[BBS_PUBKEY_LEN];
  ASSERT_TRUE(f.users.getPubkey(reader, reader_key));
  EXPECT_EQ(memcmp(f.reply.sent[0].pub_key, reader_key, BBS_PUBKEY_LEN), 0);
}

TEST(BbsNotifier, DeliversBacklogOnePostPerTick) {
  Fixture f;
  UserId author = f.registerAndLogin(3, "autore2", 1000);
  f.registerAndLogin(4, "lettore2", 1000);

  ASSERT_TRUE(f.posts.appendPost(0, author, 1000, "primo"));
  ASSERT_TRUE(f.posts.appendPost(0, author, 1010, "secondo"));

  f.notifier.tick(1020);
  ASSERT_EQ(f.reply.sent.size(), 1u);
  EXPECT_EQ(f.reply.sent[0].text, "autore2: primo");

  f.notifier.tick(1021);
  ASSERT_EQ(f.reply.sent.size(), 2u);
  EXPECT_EQ(f.reply.sent[1].text, "autore2: secondo");

  // Niente di nuovo da consegnare.
  f.notifier.tick(1022);
  EXPECT_EQ(f.reply.sent.size(), 2u);
}

TEST(BbsNotifier, AuthorNeverNotifiedOfOwnPost) {
  Fixture f;
  UserId author = f.registerAndLogin(5, "solo", 1000);  // nessun altro in Generale

  ASSERT_TRUE(f.posts.appendPost(0, author, 1000, "ciao"));
  f.notifier.tick(1001);

  EXPECT_TRUE(f.reply.sent.empty());
}

TEST(BbsNotifier, UserNotInThatRoomIsNotNotified) {
  Fixture f;
  UserId author = f.registerAndLogin(6, "autore3", 1000);
  UserId reader = f.registerAndLogin(7, "lettore3", 1000);
  ASSERT_TRUE(f.users.setCurrentRoom(reader, 1));  // il lettore e' andato altrove

  ASSERT_TRUE(f.posts.appendPost(0, author, 1000, "ciao"));
  f.notifier.tick(1001);

  EXPECT_TRUE(f.reply.sent.empty());
}

TEST(BbsNotifier, UserWithNoCurrentRoomIsNotNotified) {
  Fixture f;
  UserId author = f.registerAndLogin(8, "autore4", 1000);
  UserId reader = f.registerAndLogin(9, "lettore4", 1000);
  ASSERT_TRUE(f.users.setCurrentRoom(reader, kNoRoom));

  ASSERT_TRUE(f.posts.appendPost(0, author, 1000, "ciao"));
  f.notifier.tick(1001);

  EXPECT_TRUE(f.reply.sent.empty());
}

TEST(BbsNotifier, StopsNotifyingAfterTooManyMissedDeliveries) {
  Fixture f;
  UserId author = f.registerAndLogin(10, "autore5", 1000);
  UserId reader = f.registerAndLogin(11, "lettore5", 1000);

  for (uint8_t i = 0; i < BBS_MAX_MISSED_DELIVERIES; i++) f.sessions.recordDeliveryFailure(reader);
  ASSERT_FALSE(f.sessions.isReachable(reader));

  ASSERT_TRUE(f.posts.appendPost(0, author, 1000, "ciao"));
  f.notifier.tick(1001);

  EXPECT_TRUE(f.reply.sent.empty());
}

TEST(BbsNotifier, FailedDeliveryIsCountedAsMissedAndRetriedLater) {
  Fixture f;
  UserId author = f.registerAndLogin(12, "autore6", 1000);
  UserId reader = f.registerAndLogin(13, "lettore6", 1000);

  ASSERT_TRUE(f.posts.appendPost(0, author, 1000, "ciao"));
  f.reply.fail_next = true;
  f.notifier.tick(1001);

  EXPECT_TRUE(f.reply.sent.empty());             // l'invio e' fallito
  EXPECT_TRUE(f.sessions.isReachable(reader));   // un solo fallimento non basta a metterlo offline

  // Il puntatore "ultimo letto" non e' avanzato: al prossimo tick si riprova.
  f.notifier.tick(1002);
  ASSERT_EQ(f.reply.sent.size(), 1u);
  EXPECT_EQ(f.reply.sent[0].text, "autore6: ciao");
}

TEST(BbsNotifier, BannedUserNeverNotified) {
  Fixture f;
  UserId author = f.registerAndLogin(14, "autore7", 1000);
  UserId reader = f.registerAndLogin(15, "lettore7", 1000);
  ASSERT_TRUE(f.users.setBanned(reader, true));

  ASSERT_TRUE(f.posts.appendPost(0, author, 1000, "ciao"));
  f.notifier.tick(1001);

  EXPECT_TRUE(f.reply.sent.empty());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
