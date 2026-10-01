#include <gtest/gtest.h>

#include "bbs_fake_filesystem.h"
#include "bbs_room_state.h"

using namespace bbs;
using bbs_test::FakeFileSystem;

TEST(BbsRoomState, AllRoomsOpenByDefault) {
  FakeFileSystem fs;
  RoomState state(fs);
  EXPECT_FALSE(state.isClosed(0));
  EXPECT_FALSE(state.isClosed(2));
}

TEST(BbsRoomState, CloseThenOpenAgain) {
  FakeFileSystem fs;
  RoomState state(fs);
  ASSERT_TRUE(state.setClosed(1, true));
  EXPECT_TRUE(state.isClosed(1));
  EXPECT_FALSE(state.isClosed(0));  // le altre stanze non sono toccate

  ASSERT_TRUE(state.setClosed(1, false));
  EXPECT_FALSE(state.isClosed(1));
}

TEST(BbsRoomState, PersistsAcrossReopen) {
  FakeFileSystem fs;
  {
    RoomState state(fs);
    ASSERT_TRUE(state.setClosed(2, true));
  }
  {
    RoomState state2(fs);
    EXPECT_TRUE(state2.isClosed(2));
    EXPECT_FALSE(state2.isClosed(0));
  }
}

TEST(BbsRoomState, InvalidRoomIdIsSafe) {
  FakeFileSystem fs;
  RoomState state(fs);
  EXPECT_FALSE(state.isClosed(BBS_MAX_ROOMS));
  EXPECT_FALSE(state.setClosed(BBS_MAX_ROOMS, true));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
