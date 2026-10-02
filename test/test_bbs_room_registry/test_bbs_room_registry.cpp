#include <gtest/gtest.h>

#include <cstring>

#include "bbs_fake_filesystem.h"
#include "bbs_room_registry.h"

using namespace bbs;
using bbs_test::FakeFileSystem;

TEST(BbsRoomRegistry, DefaultsToTheThreeHistoricRooms) {
  FakeFileSystem fs;
  RoomRegistry reg(fs);
  ASSERT_EQ(reg.count(), 3u);
  EXPECT_STREQ(reg.findRoom(0)->name, "Generale");
  EXPECT_STREQ(reg.findRoom(1)->name, "Annunci");
  EXPECT_STREQ(reg.findRoom(2)->name, "Tecnico");
  EXPECT_EQ(reg.findRoom(3), nullptr);
}

TEST(BbsRoomRegistry, AddRoomPicksLowestFreeIdAndPersists) {
  FakeFileSystem fs;
  {
    RoomRegistry reg(fs);
    uint8_t id;
    ASSERT_EQ(reg.addRoom("Giardino", id), AddRoomResult::OK);
    EXPECT_EQ(id, 3);  // 0,1,2 occupati dai default
    EXPECT_EQ(reg.count(), 4u);
  }
  {
    RoomRegistry reg2(fs);  // riapertura: deve rileggere dal file
    ASSERT_EQ(reg2.count(), 4u);
    EXPECT_STREQ(reg2.findRoom(3)->name, "Giardino");
  }
}

TEST(BbsRoomRegistry, DuplicateAndInvalidNamesAreRejected) {
  FakeFileSystem fs;
  RoomRegistry reg(fs);
  uint8_t id;
  EXPECT_EQ(reg.addRoom("Generale", id), AddRoomResult::DUPLICATE_NAME);
  EXPECT_EQ(reg.addRoom("", id), AddRoomResult::INVALID_NAME);
  EXPECT_EQ(reg.addRoom("con spazi", id), AddRoomResult::INVALID_NAME);
  EXPECT_EQ(reg.count(), 3u);  // nessuna delle precedenti ha modificato l'elenco
}

TEST(BbsRoomRegistry, FullRegistryRejectsFurtherAdds) {
  FakeFileSystem fs;
  RoomRegistry reg(fs);
  uint8_t id;
  for (int i = 0; i < BBS_MAX_ROOMS - 3; i++) {
    char name[8];
    snprintf(name, sizeof(name), "r%d", i);
    ASSERT_EQ(reg.addRoom(name, id), AddRoomResult::OK);
  }
  EXPECT_EQ(reg.count(), (size_t)BBS_MAX_ROOMS);
  EXPECT_EQ(reg.addRoom("extra", id), AddRoomResult::FULL);
}

TEST(BbsRoomRegistry, RemoveRoomFreesTheIdForReuse) {
  FakeFileSystem fs;
  RoomRegistry reg(fs);
  ASSERT_EQ(reg.removeRoom(1), RemoveRoomResult::OK);
  EXPECT_EQ(reg.count(), 2u);
  EXPECT_EQ(reg.findRoom(1), nullptr);

  uint8_t id;
  ASSERT_EQ(reg.addRoom("Nuova", id), AddRoomResult::OK);
  EXPECT_EQ(id, 1);  // il piu' basso libero, riassegnato dopo la cancellazione
}

TEST(BbsRoomRegistry, RemoveUnknownIdIsNotFound) {
  FakeFileSystem fs;
  RoomRegistry reg(fs);
  EXPECT_EQ(reg.removeRoom(7), RemoveRoomResult::NOT_FOUND);
  EXPECT_EQ(reg.count(), 3u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
