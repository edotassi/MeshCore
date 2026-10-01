#include <gtest/gtest.h>

#include <string>

#include "bbs_fake_filesystem.h"
#include "bbs_mail_store.h"

using namespace bbs;
using bbs_test::FakeFileSystem;

TEST(BbsMailStore, RejectsEmptyText) {
  FakeFileSystem fs;
  MailStore store(fs);
  EXPECT_FALSE(store.send(1, 2, 100, ""));
}

TEST(BbsMailStore, RejectsTextTooLong) {
  FakeFileSystem fs;
  MailStore store(fs);
  std::string long_text(BBS_MAX_TEXT_LEN + 1, 'x');
  EXPECT_FALSE(store.send(1, 2, 100, long_text.c_str()));
}

TEST(BbsMailStore, DeliversOnlyToTheRightRecipient) {
  FakeFileSystem fs;
  MailStore store(fs);
  ASSERT_TRUE(store.send(1, 2, 100, "per te"));
  ASSERT_TRUE(store.send(1, 3, 200, "per un altro"));

  MailRecord rec;
  ASSERT_TRUE(store.findNextUnread(2, 0, rec));
  EXPECT_STREQ(rec.text, "per te");
  EXPECT_EQ(rec.sender_id, 1u);
  EXPECT_FALSE(store.findNextUnread(2, rec.timestamp, rec));

  ASSERT_TRUE(store.findNextUnread(3, 0, rec));
  EXPECT_STREQ(rec.text, "per un altro");
}

TEST(BbsMailStore, MultipleMailsToSameRecipientInOrder) {
  FakeFileSystem fs;
  MailStore store(fs);
  ASSERT_TRUE(store.send(1, 9, 100, "primo"));
  ASSERT_TRUE(store.send(2, 9, 200, "secondo"));

  MailRecord rec;
  ASSERT_TRUE(store.findNextUnread(9, 0, rec));
  EXPECT_STREQ(rec.text, "primo");
  ASSERT_TRUE(store.findNextUnread(9, rec.timestamp, rec));
  EXPECT_STREQ(rec.text, "secondo");
  EXPECT_FALSE(store.findNextUnread(9, rec.timestamp, rec));
}

TEST(BbsMailStore, CountUnreadMatchesFindNextUnreadScan) {
  FakeFileSystem fs;
  MailStore store(fs);
  ASSERT_TRUE(store.send(1, 9, 100, "uno"));
  ASSERT_TRUE(store.send(2, 9, 200, "due"));
  ASSERT_TRUE(store.send(1, 5, 100, "per un altro"));

  EXPECT_EQ(store.countUnread(9, 0), 2u);
  EXPECT_EQ(store.countUnread(9, 100), 1u);
  EXPECT_EQ(store.countUnread(5, 0), 1u);
}

TEST(BbsMailStore, RepairOnMissingFileIsNoop) {
  FakeFileSystem fs;
  MailStore store(fs);
  EXPECT_TRUE(store.repair());
}

TEST(BbsMailStore, CorruptedTailIsRepairedAndSendStillWorksAfter) {
  FakeFileSystem fs;
  MailStore store(fs);

  ASSERT_TRUE(store.send(1, 2, 1000, "prima mail"));

  IFile* f = fs.open("/bbs/mail.log", 'a');
  ASSERT_TRUE(f->valid());
  uint8_t garbage[6] = {1, 0, 0, 0, 0, 0};  // intestazione troncata, mai completata
  f->write(garbage, sizeof(garbage));
  f->close();

  ASSERT_TRUE(store.repair());

  MailRecord rec;
  ASSERT_TRUE(store.findNextUnread(2, 0, rec));
  EXPECT_STREQ(rec.text, "prima mail");
  EXPECT_FALSE(store.findNextUnread(2, rec.timestamp, rec));

  ASSERT_TRUE(store.send(1, 2, 2000, "seconda mail"));
  MailRecord rec2;
  ASSERT_TRUE(store.findNextUnread(2, 1000, rec2));
  EXPECT_STREQ(rec2.text, "seconda mail");
}

TEST(BbsMailStore, TotalCountIgnoresRecipient) {
  FakeFileSystem fs;
  MailStore store(fs);
  EXPECT_EQ(store.totalCount(), 0u);
  ASSERT_TRUE(store.send(1, 2, 100, "a"));
  ASSERT_TRUE(store.send(1, 3, 200, "b"));
  ASSERT_TRUE(store.send(2, 3, 300, "c"));
  EXPECT_EQ(store.totalCount(), 3u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
