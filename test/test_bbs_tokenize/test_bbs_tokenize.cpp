#include <gtest/gtest.h>

#include <cstring>

#include "bbs_tokenize.h"

using namespace bbs;

TEST(BbsTokenize, SimpleCommandNoArgs) {
  char cmd[16];
  const char* args = extractCommand("H", cmd, sizeof(cmd));
  EXPECT_STREQ("H", cmd);
  EXPECT_STREQ("", args);
}

TEST(BbsTokenize, CommandWithArgs) {
  char cmd[16];
  const char* args = extractCommand("REGISTER mario", cmd, sizeof(cmd));
  EXPECT_STREQ("REGISTER", cmd);
  EXPECT_STREQ("mario", args);
}

TEST(BbsTokenize, LowercaseCommandUppercased) {
  char cmd[16];
  const char* args = extractCommand("register mario", cmd, sizeof(cmd));
  EXPECT_STREQ("REGISTER", cmd);
  EXPECT_STREQ("mario", args);
}

TEST(BbsTokenize, LeadingWhitespaceSkipped) {
  char cmd[16];
  const char* args = extractCommand("   login", cmd, sizeof(cmd));
  EXPECT_STREQ("LOGIN", cmd);
  EXPECT_STREQ("", args);
}

TEST(BbsTokenize, MultipleSpacesBetweenTokens) {
  char cmd[16];
  const char* args = extractCommand("register   mario", cmd, sizeof(cmd));
  EXPECT_STREQ("REGISTER", cmd);
  EXPECT_STREQ("mario", args);
}

TEST(BbsTokenize, EmptyInput) {
  char cmd[16];
  const char* args = extractCommand("", cmd, sizeof(cmd));
  EXPECT_STREQ("", cmd);
  EXPECT_STREQ("", args);
}

TEST(BbsTokenize, CommandTooLongIsTruncatedButArgsStillFound) {
  char cmd[8];  // capacita' piccola apposta
  const char* args = extractCommand("REGISTERLONGNAME mario", cmd, sizeof(cmd));
  EXPECT_EQ(strlen(cmd), 7u);  // troncato a cmd_cap-1
  EXPECT_STREQ("mario", args);
}

TEST(BbsTokenize, ExtractTokenDoesNotUppercase) {
  char tok[16];
  const char* rest = extractToken("Mario ciao a tutti", tok, sizeof(tok));
  EXPECT_STREQ("Mario", tok);
  EXPECT_STREQ("ciao a tutti", rest);
}

TEST(BbsTokenize, ExtractTokenEmptyWhenNoInput) {
  char tok[16];
  const char* rest = extractToken("", tok, sizeof(tok));
  EXPECT_STREQ("", tok);
  EXPECT_STREQ("", rest);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
