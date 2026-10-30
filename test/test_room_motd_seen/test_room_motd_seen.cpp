#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "bbs_fake_filesystem.h"
#include "room_motd_seen.h"

using namespace room;
using bbs_test::FakeFileSystem;

namespace {
void key(uint8_t* k, uint32_t n) {
  memset(k, 0, kMotdKeyPrefixLen);
  memcpy(k, &n, 4);
}
const uint32_t H = 1234;
const uint32_t T0 = 1800000000u;

void writeBytes(FakeFileSystem& fs, const char* path, const std::vector<uint8_t>& v) {
  bbs::IFile* f = fs.open(path, 'w');
  ASSERT_TRUE(f && f->valid());
  if (!v.empty()) f->write(v.data(), v.size());
  f->close();
}
} // namespace

TEST(RoomMotdSeen, FirstTimeSends) {
  FakeFileSystem fs;
  MotdSeen m(fs);
  uint8_t k[8]; key(k, 1);
  EXPECT_TRUE(m.shouldSend(k, H, T0));
}

TEST(RoomMotdSeen, SameHashWithin24hSkips) {
  FakeFileSystem fs;
  MotdSeen m(fs);
  uint8_t k[8]; key(k, 1);
  m.markSent(k, H, T0);
  EXPECT_TRUE(m.dirty());
  EXPECT_FALSE(m.shouldSend(k, H, T0 + 3600));
  EXPECT_FALSE(m.shouldSend(k, H, T0 + 86399));
}

TEST(RoomMotdSeen, ChangedHashSends) {
  FakeFileSystem fs;
  MotdSeen m(fs);
  uint8_t k[8]; key(k, 1);
  m.markSent(k, H, T0);
  EXPECT_TRUE(m.shouldSend(k, H + 1, T0 + 10));
}

TEST(RoomMotdSeen, Exactly24hSends) {
  FakeFileSystem fs;
  MotdSeen m(fs);
  uint8_t k[8]; key(k, 1);
  m.markSent(k, H, T0);
  EXPECT_TRUE(m.shouldSend(k, H, T0 + 86400));
}

TEST(RoomMotdSeen, ClockWentBackwardsSkips) {
  FakeFileSystem fs;
  MotdSeen m(fs);
  uint8_t k[8]; key(k, 1);
  m.markSent(k, H, T0);
  EXPECT_FALSE(m.shouldSend(k, H, T0 - 5000));
  EXPECT_TRUE(m.shouldSend(k, H + 1, T0 - 5000));  // testo nuovo: invia comunque
}

TEST(RoomMotdSeen, UpdateExistingDoesNotGrow) {
  FakeFileSystem fs;
  MotdSeen m(fs);
  uint8_t k[8]; key(k, 1);
  m.markSent(k, H, T0);
  m.markSent(k, H, T0 + 90000);
  EXPECT_EQ(m.count(), 1u);
  EXPECT_FALSE(m.shouldSend(k, H, T0 + 90001));
}

TEST(RoomMotdSeen, EvictsOldestWhenFull) {
  FakeFileSystem fs;
  MotdSeen m(fs);
  uint8_t k[8];
  for (uint32_t i = 0; i < kMotdSeenCapacity; i++) {
    key(k, i + 1);
    m.markSent(k, H, T0 + 100 + i);   // i = 0 e' il piu' vecchio
  }
  EXPECT_EQ(m.count(), kMotdSeenCapacity);
  key(k, 9999);
  m.markSent(k, H, T0 + 5000);
  EXPECT_EQ(m.count(), kMotdSeenCapacity);

  uint8_t oldest[8]; key(oldest, 1);
  uint8_t second[8]; key(second, 2);
  EXPECT_TRUE(m.shouldSend(oldest, H, T0 + 5001));   // sfrattato
  EXPECT_FALSE(m.shouldSend(second, H, T0 + 5001));  // ancora presente
  EXPECT_FALSE(m.shouldSend(k, H, T0 + 5001));
}

TEST(RoomMotdSeen, SaveLoadRoundTrip) {
  FakeFileSystem fs;
  {
    MotdSeen m(fs);
    uint8_t k[8];
    key(k, 1); m.markSent(k, H, T0);
    key(k, 2); m.markSent(k, H + 1, T0 + 10);
    ASSERT_TRUE(m.save());
    EXPECT_FALSE(m.dirty());
  }
  MotdSeen m2(fs);
  m2.load();
  EXPECT_EQ(m2.count(), 2u);
  EXPECT_FALSE(m2.dirty());
  uint8_t k[8];
  key(k, 1);
  EXPECT_FALSE(m2.shouldSend(k, H, T0 + 100));
  EXPECT_TRUE(m2.shouldSend(k, H + 5, T0 + 100));
  key(k, 2);
  EXPECT_FALSE(m2.shouldSend(k, H + 1, T0 + 100));
  key(k, 3);
  EXPECT_TRUE(m2.shouldSend(k, H, T0 + 100));
}

TEST(RoomMotdSeen, MissingAndGarbageGiveEmpty) {
  FakeFileSystem fs;
  MotdSeen m(fs);
  m.load();
  EXPECT_EQ(m.count(), 0u);

  writeBytes(fs, kMotdSeenPath, std::vector<uint8_t>(80, 0xAB));
  m.load();
  EXPECT_EQ(m.count(), 0u);

  uint8_t k[8]; key(k, 1);
  m.markSent(k, H, T0);
  ASSERT_TRUE(m.save());
  // versione ignota
  bbs::IFile* f = fs.open(kMotdSeenPath, 'r');
  std::vector<uint8_t> data(f->size());
  f->read(data.data(), data.size());
  f->close();
  data[4] = 77;
  writeBytes(fs, kMotdSeenPath, data);
  m.load();
  EXPECT_EQ(m.count(), 0u);
}

TEST(RoomMotdSeen, TmpFallbackWhenMainMissing) {
  FakeFileSystem fs;
  MotdSeen m(fs);
  uint8_t k[8]; key(k, 1);
  m.markSent(k, H, T0);
  ASSERT_TRUE(m.save());
  ASSERT_TRUE(fs.rename(kMotdSeenPath, kMotdSeenTmpPath));  // simula spegnimento tra remove e rename

  MotdSeen m2(fs);
  m2.load();
  EXPECT_EQ(m2.count(), 1u);
  EXPECT_FALSE(m2.shouldSend(k, H, T0 + 1));
}

TEST(RoomMotdSeen, HashStableAndDistinct) {
  EXPECT_EQ(motdHash(""), 2166136261u);
  EXPECT_EQ(motdHash("a"), 0xE40C292Cu);   // FNV-1a 32 bit noto
  EXPECT_EQ(motdHash("Benvenuti"), motdHash("Benvenuti"));
  EXPECT_NE(motdHash("Benvenuti"), motdHash("Benvenuti!"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
