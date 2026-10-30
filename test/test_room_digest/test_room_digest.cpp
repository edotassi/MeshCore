#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "room_digest.h"

using namespace room;

namespace {

const uint32_t kNow = 1800000000u;

struct Posts {
  std::vector<uint32_t> ts;
  std::vector<uint8_t> keys;
  void add(uint32_t t, uint8_t author) {
    ts.push_back(t);
    for (size_t i = 0; i < kAuthorPrefixLen; i++) keys.push_back(author);
  }
};

uint8_t own[32];

DigestCounts count(const Posts& p, uint32_t now = kNow) {
  return countRecent(p.ts.data(), p.keys.data(), p.ts.size(), own, now);
}

} // namespace

class RoomDigest : public ::testing::Test {
protected:
  void SetUp() override { memset(own, 0xEE, sizeof(own)); }
};

TEST_F(RoomDigest, NoPostsGivesNothing) {
  Posts p;
  char out[64] = "x";
  EXPECT_EQ(formatDigest(p.ts.data(), p.keys.data(), 0, own, kNow, out, sizeof(out)), 0u);
  EXPECT_STREQ(out, "");
}

TEST_F(RoomDigest, CountsPostsAndDistinctAuthors) {
  Posts p;
  p.add(kNow - 10, 1);
  p.add(kNow - 20, 1);
  p.add(kNow - 30, 2);
  p.add(kNow - 40, 3);
  DigestCounts c = count(p);
  EXPECT_EQ(c.posts, 4u);
  EXPECT_EQ(c.authors, 3u);

  char out[64];
  size_t n = formatDigest(p.ts.data(), p.keys.data(), p.ts.size(), own, kNow, out, sizeof(out));
  EXPECT_STREQ(out, "Ultime 24h: 4 post da 3 utenti");
  EXPECT_EQ(n, strlen(out));
}

TEST_F(RoomDigest, SingularForm) {
  Posts p;
  p.add(kNow - 5, 1);
  char out[64];
  formatDigest(p.ts.data(), p.keys.data(), 1, own, kNow, out, sizeof(out));
  EXPECT_STREQ(out, "Ultime 24h: 1 post da 1 utente");
}

TEST_F(RoomDigest, OwnPostsCountAsPostsButNotAuthors) {
  Posts p;
  p.add(kNow - 10, 0xEE);
  p.add(kNow - 20, 0xEE);
  p.add(kNow - 30, 7);
  DigestCounts c = count(p);
  EXPECT_EQ(c.posts, 3u);
  EXPECT_EQ(c.authors, 1u);

  Posts only_own;
  only_own.add(kNow - 10, 0xEE);
  c = count(only_own);
  EXPECT_EQ(c.posts, 1u);
  EXPECT_EQ(c.authors, 0u);
  char out[64];
  formatDigestText(c, out, sizeof(out));
  EXPECT_STREQ(out, "Ultime 24h: 1 post");
}

TEST_F(RoomDigest, TwentyFourHourBoundary) {
  Posts p;
  p.add(kNow - (kDigestWindowSecs - 1), 1);  // dentro
  p.add(kNow - kDigestWindowSecs, 2);        // esattamente 24 h fa: fuori
  p.add(kNow - kDigestWindowSecs - 1, 3);    // fuori
  DigestCounts c = count(p);
  EXPECT_EQ(c.posts, 1u);
  EXPECT_EQ(c.authors, 1u);
}

TEST_F(RoomDigest, OldPostsDoNotContributeAuthors) {
  Posts p;
  p.add(kNow - 3 * kDigestWindowSecs, 1);
  p.add(kNow - 100, 1);
  DigestCounts c = count(p);
  EXPECT_EQ(c.posts, 1u);
  EXPECT_EQ(c.authors, 1u);
}

TEST_F(RoomDigest, AllOldAndEmptySlotsGiveNothing) {
  Posts p;
  p.add(0, 1);  // slot vuoto dell'anello
  p.add(kNow - 2 * kDigestWindowSecs, 2);
  char out[64];
  EXPECT_EQ(formatDigest(p.ts.data(), p.keys.data(), p.ts.size(), own, kNow, out, sizeof(out)), 0u);
}

TEST_F(RoomDigest, ParseHourMinute) {
  uint8_t h, m;
  EXPECT_TRUE(parseHourMinute("08:30", h, m));
  EXPECT_EQ(h, 8);
  EXPECT_EQ(m, 30);
  EXPECT_TRUE(parseHourMinute("7:05", h, m));
  EXPECT_FALSE(parseHourMinute("24:00", h, m));
  EXPECT_FALSE(parseHourMinute("12:60", h, m));
  EXPECT_FALSE(parseHourMinute("1230", h, m));
}

TEST_F(RoomDigest, ParseTzOffset) {
  int16_t o;
  EXPECT_TRUE(parseTzOffset("+1", o));
  EXPECT_EQ(o, 60);
  EXPECT_TRUE(parseTzOffset("-5:30", o));
  EXPECT_EQ(o, -330);
  EXPECT_TRUE(parseTzOffset("14", o));
  EXPECT_EQ(o, 840);
  EXPECT_FALSE(parseTzOffset("+14:01", o));
  EXPECT_FALSE(parseTzOffset("-12:01", o));
  EXPECT_FALSE(parseTzOffset("abc", o));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
