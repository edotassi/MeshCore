#include <gtest/gtest.h>

#include "bbs_fake_filesystem.h"
#include "bbs_motd.h"

using namespace bbs;
using bbs_test::FakeFileSystem;

TEST(BbsMotd, EmptyByDefault) {
  FakeFileSystem fs;
  MotdStore motd(fs);
  char buf[BBS_MAX_TEXT_LEN + 1];
  ASSERT_TRUE(motd.get(buf, sizeof(buf)));
  EXPECT_STREQ(buf, "");
}

TEST(BbsMotd, SetThenGet) {
  FakeFileSystem fs;
  MotdStore motd(fs);
  ASSERT_TRUE(motd.set("Manutenzione stasera alle 21."));
  char buf[BBS_MAX_TEXT_LEN + 1];
  ASSERT_TRUE(motd.get(buf, sizeof(buf)));
  EXPECT_STREQ(buf, "Manutenzione stasera alle 21.");
}

TEST(BbsMotd, ClearingWithEmptyStringRemovesIt) {
  FakeFileSystem fs;
  MotdStore motd(fs);
  ASSERT_TRUE(motd.set("qualcosa"));
  ASSERT_TRUE(motd.set(""));
  char buf[BBS_MAX_TEXT_LEN + 1];
  ASSERT_TRUE(motd.get(buf, sizeof(buf)));
  EXPECT_STREQ(buf, "");
}

TEST(BbsMotd, PersistsAcrossReopen) {
  FakeFileSystem fs;
  { MotdStore motd(fs); motd.set("bentornati"); }
  {
    MotdStore motd2(fs);
    char buf[BBS_MAX_TEXT_LEN + 1];
    ASSERT_TRUE(motd2.get(buf, sizeof(buf)));
    EXPECT_STREQ(buf, "bentornati");
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
