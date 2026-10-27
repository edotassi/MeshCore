#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "bbs_news_broadcast.h"

using namespace bbs;

TEST(BbsNewsBroadcast, EmptyQueueGivesNothing) {
  NewsBroadcastQueue q;
  char out[64];
  EXPECT_FALSE(q.poll(0, out, sizeof(out)));
}

TEST(BbsNewsBroadcast, FirstIsImmediateThenSpacedBy30s) {
  NewsBroadcastQueue q;
  q.push("[A] uno");
  q.push("[B] due");
  q.push("[C] tre");
  char out[64];
  ASSERT_TRUE(q.poll(1000, out, sizeof(out)));
  EXPECT_STREQ(out, "[A] uno");
  EXPECT_FALSE(q.poll(1000, out, sizeof(out)));
  EXPECT_FALSE(q.poll(30999, out, sizeof(out)));
  ASSERT_TRUE(q.poll(31000, out, sizeof(out)));
  EXPECT_STREQ(out, "[B] due");
  EXPECT_FALSE(q.poll(60999, out, sizeof(out)));
  ASSERT_TRUE(q.poll(61000, out, sizeof(out)));
  EXPECT_STREQ(out, "[C] tre");
  EXPECT_FALSE(q.poll(200000, out, sizeof(out)));
}

TEST(BbsNewsBroadcast, SpacingAppliesToItemsPushedLater) {
  NewsBroadcastQueue q;
  char out[64];
  q.push("[A] uno");
  ASSERT_TRUE(q.poll(5000, out, sizeof(out)));
  q.push("[B] due");
  EXPECT_FALSE(q.poll(10000, out, sizeof(out)));
  EXPECT_TRUE(q.poll(35000, out, sizeof(out)));
}

TEST(BbsNewsBroadcast, MillisWraparound) {
  NewsBroadcastQueue q;
  char out[64];
  q.push("[A] uno");
  q.push("[B] due");
  ASSERT_TRUE(q.poll(0xFFFFFFFFu - 10000u, out, sizeof(out)));
  EXPECT_FALSE(q.poll(0xFFFFFFFFu, out, sizeof(out)));
  EXPECT_FALSE(q.poll(10000, out, sizeof(out)));  // 10001+10000 ms dopo
  EXPECT_TRUE(q.poll(20000, out, sizeof(out)));   // > 30 s dopo
}

TEST(BbsNewsBroadcast, FullQueueDropsOldest) {
  NewsBroadcastQueue q;
  for (size_t i = 0; i < kNewsBroadcastCapacity + 3; i++) q.push(("n" + std::to_string(i)).c_str());
  EXPECT_EQ(q.size(), kNewsBroadcastCapacity);
  char out[16];
  uint32_t t = 0;
  for (size_t i = 3; i < kNewsBroadcastCapacity + 3; i++) {
    ASSERT_TRUE(q.poll(t, out, sizeof(out)));
    EXPECT_EQ(std::string(out), "n" + std::to_string(i));
    t += kNewsBroadcastIntervalMs;
  }
  EXPECT_FALSE(q.poll(t, out, sizeof(out)));
}

TEST(BbsNewsBroadcast, ClearDropsPending) {
  NewsBroadcastQueue q;
  q.push("[A] uno");
  q.clear();
  EXPECT_EQ(q.size(), 0u);
  char out[16];
  EXPECT_FALSE(q.poll(0, out, sizeof(out)));
  q.push("[B] due");  // dopo clear la coda funziona ancora
  ASSERT_TRUE(q.poll(0, out, sizeof(out)));
  EXPECT_STREQ(out, "[B] due");
}

TEST(BbsNewsBroadcast, TruncatesOnUtf8Boundary) {
  NewsBroadcastQueue q;
  q.push("[ANSA] \xC3\xA8\xC3\xA8\xC3\xA8");  // 8 byte di prefisso + 6
  char out[11];
  ASSERT_TRUE(q.poll(0, out, sizeof(out)));
  // cap-1 = 10 byte: il terzo "e'" (byte 8..9) entra, il secondo no se tagliato a meta'
  EXPECT_STREQ(out, "[ANSA] \xC3\xA8");
  q.push("[ANSA] \xC3\xA8\xC3\xA8");
  char out2[10];  // 9 byte: taglia a meta' del secondo carattere
  ASSERT_TRUE(q.poll(kNewsBroadcastIntervalMs, out2, sizeof(out2)));
  EXPECT_STREQ(out2, "[ANSA] \xC3\xA8");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
