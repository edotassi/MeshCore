#include <gtest/gtest.h>

#include "bbs_fake_filesystem.h"
#include "bbs_user_store.h"

using namespace bbs;
using bbs_test::FakeFileSystem;

namespace {

void makeKey(uint8_t out[BBS_PUBKEY_LEN], uint8_t seed) {
  for (size_t i = 0; i < BBS_PUBKEY_LEN; i++) out[i] = (uint8_t)(seed + i);
}

} // namespace

TEST(BbsUserStore, FindOnEmptyStoreReturnsInvalid) {
  FakeFileSystem fs;
  UserStore store(fs);
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 1);
  EXPECT_EQ(store.findByPubkey(key), kInvalidUserId);
  EXPECT_EQ(store.count(), 0u);
}

TEST(BbsUserStore, RegisterThenFind) {
  FakeFileSystem fs;
  UserStore store(fs);
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 1);
  UserId id;
  auto r = store.registerUser(key, "mario", 1000, id);
  EXPECT_EQ(r, RegisterResult::OK);
  EXPECT_EQ(id, 0u);
  EXPECT_EQ(store.count(), 1u);
  EXPECT_EQ(store.findByPubkey(key), 0u);

  char nick[BBS_NICK_LEN];
  ASSERT_TRUE(store.getNickname(0, nick, sizeof(nick)));
  EXPECT_STREQ("mario", nick);
}

TEST(BbsUserStore, RegisterTwiceSamePubkeyFails) {
  FakeFileSystem fs;
  UserStore store(fs);
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 2);
  UserId id;
  ASSERT_EQ(store.registerUser(key, "ada", 1000, id), RegisterResult::OK);
  UserId id2;
  EXPECT_EQ(store.registerUser(key, "adalinda", 1001, id2), RegisterResult::ALREADY_EXISTS);
}

TEST(BbsUserStore, RegisterInvalidNickname) {
  FakeFileSystem fs;
  UserStore store(fs);
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 3);
  UserId id;
  EXPECT_EQ(store.registerUser(key, "", 1000, id), RegisterResult::INVALID_NICKNAME);
  EXPECT_EQ(store.registerUser(key, "has space", 1000, id), RegisterResult::INVALID_NICKNAME);
  EXPECT_EQ(store.registerUser(key, "waytoolongnickname12345", 1000, id), RegisterResult::INVALID_NICKNAME);
}

TEST(BbsUserStore, MultipleUsersDistinctIds) {
  FakeFileSystem fs;
  UserStore store(fs);
  uint8_t key1[BBS_PUBKEY_LEN], key2[BBS_PUBKEY_LEN], key3[BBS_PUBKEY_LEN];
  makeKey(key1, 10);
  makeKey(key2, 20);
  makeKey(key3, 30);
  UserId id1, id2, id3;
  ASSERT_EQ(store.registerUser(key1, "uno", 100, id1), RegisterResult::OK);
  ASSERT_EQ(store.registerUser(key2, "due", 200, id2), RegisterResult::OK);
  ASSERT_EQ(store.registerUser(key3, "tre", 300, id3), RegisterResult::OK);
  EXPECT_EQ(id1, 0u);
  EXPECT_EQ(id2, 1u);
  EXPECT_EQ(id3, 2u);
  EXPECT_EQ(store.findByPubkey(key2), 1u);
}

TEST(BbsUserStore, TouchLoginPersistsAcrossReopen) {
  FakeFileSystem fs;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 5);
  UserId id;
  {
    UserStore store(fs);
    ASSERT_EQ(store.registerUser(key, "carla", 100, id), RegisterResult::OK);
    ASSERT_TRUE(store.touchLogin(id, 555));
  }
  {
    UserStore store2(fs);  // stessa fs, nuova istanza: verifica persistenza reale
    EXPECT_EQ(store2.findByPubkey(key), id);
  }
}

TEST(BbsUserStore, NicknameBoundary15CharsOk16Fails) {
  FakeFileSystem fs;
  UserStore store(fs);
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 7);
  UserId id;
  EXPECT_EQ(store.registerUser(key, "abcdefghij12345", 100, id), RegisterResult::OK);  // 15 char
  uint8_t key2[BBS_PUBKEY_LEN];
  makeKey(key2, 8);
  UserId id2;
  EXPECT_EQ(store.registerUser(key2, "abcdefghij123456", 100, id2), RegisterResult::INVALID_NICKNAME);  // 16 char
}

TEST(BbsUserStore, DefaultSubscriptionsOnRegister) {
  FakeFileSystem fs;
  UserStore store(fs);
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 20);
  UserId id;
  ASSERT_EQ(store.registerUser(key, "gino", 100, id), RegisterResult::OK);
  EXPECT_TRUE(store.isSubscribed(id, 0));   // Generale: di default
  EXPECT_TRUE(store.isSubscribed(id, 1));   // Annunci: di default
  EXPECT_FALSE(store.isSubscribed(id, 2));  // Tecnico: opt-in
}

TEST(BbsUserStore, SubscribeUnsubscribePersistAcrossReopen) {
  FakeFileSystem fs;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 21);
  UserId id;
  {
    UserStore store(fs);
    ASSERT_EQ(store.registerUser(key, "lara", 100, id), RegisterResult::OK);
    ASSERT_TRUE(store.setSubscribed(id, 2, true));
    ASSERT_TRUE(store.setSubscribed(id, 0, false));
  }
  {
    UserStore store2(fs);
    EXPECT_TRUE(store2.isSubscribed(id, 2));
    EXPECT_FALSE(store2.isSubscribed(id, 0));
    EXPECT_TRUE(store2.isSubscribed(id, 1));  // invariata
  }
}

TEST(BbsUserStore, SubscribeInvalidRoomIdFails) {
  FakeFileSystem fs;
  UserStore store(fs);
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 22);
  UserId id;
  ASSERT_EQ(store.registerUser(key, "nico", 100, id), RegisterResult::OK);
  EXPECT_FALSE(store.setSubscribed(id, BBS_MAX_ROOMS, true));
  EXPECT_FALSE(store.isSubscribed(id, BBS_MAX_ROOMS));
}

TEST(BbsUserStore, FindByNicknameWorks) {
  FakeFileSystem fs;
  UserStore store(fs);
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 23);
  UserId id;
  ASSERT_EQ(store.registerUser(key, "silvia", 100, id), RegisterResult::OK);
  EXPECT_EQ(store.findByNickname("silvia"), id);
  EXPECT_EQ(store.findByNickname("nonexiste"), kInvalidUserId);
}

TEST(BbsUserStore, FirstRegisteredUserIsAdminRestAreUsers) {
  FakeFileSystem fs;
  UserStore store(fs);
  uint8_t key1[BBS_PUBKEY_LEN], key2[BBS_PUBKEY_LEN];
  makeKey(key1, 40);
  makeKey(key2, 41);
  UserId id1, id2;
  ASSERT_EQ(store.registerUser(key1, "primo", 100, id1), RegisterResult::OK);
  ASSERT_EQ(store.registerUser(key2, "secondo", 100, id2), RegisterResult::OK);
  EXPECT_EQ(store.getRole(id1), ROLE_ADMIN);
  EXPECT_EQ(store.getRole(id2), ROLE_USER);
}

TEST(BbsUserStore, SetRolePersistsAcrossReopen) {
  FakeFileSystem fs;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 42);
  UserId id;
  {
    UserStore store(fs);
    ASSERT_EQ(store.registerUser(key, "utente", 100, id), RegisterResult::OK);
    ASSERT_TRUE(store.setRole(id, ROLE_MODERATOR));
  }
  {
    UserStore store2(fs);
    EXPECT_EQ(store2.getRole(id), ROLE_MODERATOR);
  }
}

TEST(BbsUserStore, BanAndMuteFlagsAreIndependentAndPersist) {
  FakeFileSystem fs;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 43);
  UserId id;
  {
    UserStore store(fs);
    ASSERT_EQ(store.registerUser(key, "birbante", 100, id), RegisterResult::OK);
    EXPECT_FALSE(store.isBanned(id));
    EXPECT_FALSE(store.isMuted(id));
    ASSERT_TRUE(store.setMuted(id, true));
  }
  {
    UserStore store2(fs);
    EXPECT_TRUE(store2.isMuted(id));
    EXPECT_FALSE(store2.isBanned(id));  // indipendente da muted
    ASSERT_TRUE(store2.setBanned(id, true));
  }
  {
    UserStore store3(fs);
    EXPECT_TRUE(store3.isBanned(id));
    EXPECT_TRUE(store3.isMuted(id));  // non toccato dal set di banned
    ASSERT_TRUE(store3.setMuted(id, false));
  }
  {
    UserStore store4(fs);
    EXPECT_TRUE(store4.isBanned(id));
    EXPECT_FALSE(store4.isMuted(id));
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
