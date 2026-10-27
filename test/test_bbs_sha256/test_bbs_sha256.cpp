#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "bbs_sha256.h"

using namespace bbs;

namespace {

std::string hex(const uint8_t* p, size_t n) {
  static const char k[] = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; i++) {
    s += k[p[i] >> 4];
    s += k[p[i] & 15];
  }
  return s;
}

std::string shaHex(const std::string& in) {
  uint8_t d[32];
  sha256((const uint8_t*)in.data(), in.size(), d);
  return hex(d, 32);
}

std::string hmacHex(const std::vector<uint8_t>& key, const std::string& msg) {
  uint8_t d[32];
  hmacSha256(key.data(), key.size(), (const uint8_t*)msg.data(), msg.size(), d);
  return hex(d, 32);
}

} // namespace

TEST(BbsSha256, FipsVectors) {
  EXPECT_EQ(shaHex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(shaHex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(shaHex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(BbsSha256, MillionAs) {
  EXPECT_EQ(shaHex(std::string(1000000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(BbsSha256, IncrementalMatchesOneShotAcrossBlockBoundaries) {
  std::string in;
  for (int i = 0; i < 300; i++) in += (char)('a' + i % 26);
  for (size_t chunk : {1u, 7u, 63u, 64u, 65u, 200u}) {
    Sha256 s;
    for (size_t off = 0; off < in.size(); off += chunk) {
      s.update((const uint8_t*)in.data() + off, std::min(chunk, in.size() - off));
    }
    uint8_t d[32];
    s.finish(d);
    EXPECT_EQ(hex(d, 32), shaHex(in)) << chunk;
  }
}

TEST(BbsHmacSha256, Rfc4231Case1) {
  std::vector<uint8_t> key(20, 0x0b);
  EXPECT_EQ(hmacHex(key, "Hi There"), "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

TEST(BbsHmacSha256, Rfc4231Case2) {
  std::string k = "Jefe";
  std::vector<uint8_t> key(k.begin(), k.end());
  EXPECT_EQ(hmacHex(key, "what do ya want for nothing?"),
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST(BbsHmacSha256, Rfc4231Case6LongKeyIsHashed) {
  std::vector<uint8_t> key(131, 0xaa);
  EXPECT_EQ(hmacHex(key, "Test Using Larger Than Block-Size Key - Hash Key First"),
            "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

TEST(BbsHmacSha256, ProtocolVector) {
  std::vector<uint8_t> key(32);
  for (size_t i = 0; i < 32; i++) key[i] = (uint8_t)i;
  EXPECT_EQ(hmacHex(key,
                    "v1\n3f2a9c1d7e8b4a60\n1793091600\nANSA\nTerremoto di magnitudo 3.2 al largo di Otranto, "
                    "nessun danno"),
            "0bb6e86b3ce7631bdd0382262ea63d32da0fcbf9e364b771d8e893976bfd91cd");
}

TEST(BbsSha256, ConstantTimeEqual) {
  uint8_t a[4] = {1, 2, 3, 4}, b[4] = {1, 2, 3, 4}, c[4] = {1, 2, 3, 5};
  EXPECT_TRUE(constantTimeEqual(a, b, 4));
  EXPECT_FALSE(constantTimeEqual(a, c, 4));
  EXPECT_TRUE(constantTimeEqual(a, c, 3));
  EXPECT_TRUE(constantTimeEqual(a, c, 0));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
