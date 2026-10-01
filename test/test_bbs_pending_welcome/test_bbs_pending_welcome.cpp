#include <gtest/gtest.h>

#include "bbs_fake_filesystem.h"
#include "bbs_pending_welcome.h"

using namespace bbs;
using bbs_test::FakeFileSystem;

namespace {

void makeKey(uint8_t out[BBS_PUBKEY_LEN], uint8_t seed) {
  for (size_t i = 0; i < BBS_PUBKEY_LEN; i++) out[i] = (uint8_t)(seed + i);
}

} // namespace

TEST(BbsPendingWelcome, NotWelcomedInitially) {
  FakeFileSystem fs;
  PendingWelcomeTable table(fs);
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 1);
  EXPECT_FALSE(table.isWelcomed(key));
}

TEST(BbsPendingWelcome, MarkThenWelcomed) {
  FakeFileSystem fs;
  PendingWelcomeTable table(fs);
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 2);
  EXPECT_TRUE(table.markWelcomed(key));
  EXPECT_TRUE(table.isWelcomed(key));
}

TEST(BbsPendingWelcome, DistinctKeysIndependent) {
  FakeFileSystem fs;
  PendingWelcomeTable table(fs);
  uint8_t key1[BBS_PUBKEY_LEN], key2[BBS_PUBKEY_LEN];
  makeKey(key1, 3);
  makeKey(key2, 4);
  table.markWelcomed(key1);
  EXPECT_TRUE(table.isWelcomed(key1));
  EXPECT_FALSE(table.isWelcomed(key2));
}

TEST(BbsPendingWelcome, PersistsAcrossReopen) {
  FakeFileSystem fs;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 5);
  {
    PendingWelcomeTable table(fs);
    table.markWelcomed(key);
  }
  {
    PendingWelcomeTable table2(fs);
    EXPECT_TRUE(table2.isWelcomed(key));
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
