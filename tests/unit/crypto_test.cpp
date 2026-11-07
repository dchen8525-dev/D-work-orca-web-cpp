#include <orca/crypto/base64.hpp>
#include <orca/crypto/bytes.hpp>
#include <orca/crypto/hkdf.hpp>
#include <orca/crypto/hmac.hpp>
#include <orca/crypto/sha256.hpp>

#include "test_util.hpp"

#include <string>

using namespace orca::crypto;

namespace {

Bytes toBytes(std::string_view value) {
    Bytes out;
    appendRaw(out, value);
    return out;
}

bool checkHash(std::string_view input, const std::string& expectedHex, const std::string& label) {
    const Hash256 digest = sha256(input);
    return test::checkBytes(Bytes(digest.begin(), digest.end()), expectedHex, label);
}

}  // namespace

int main() {
    // FIPS 180-4 / standard SHA-256 vectors.
    checkHash("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "sha256 empty");
    checkHash("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "sha256 abc");
    checkHash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
              "sha256 56-byte vector");

    // RFC 4231 HMAC-SHA256 test cases 1 and 2.
    const Hash256 hmac1 = hmacSha256(Bytes(20, 0x0b), toBytes("Hi There"));
    test::checkBytes(Bytes(hmac1.begin(), hmac1.end()),
                     "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
                     "HMAC-SHA256 RFC4231 case 1");
    const Hash256 hmac2 = hmacSha256(toBytes("Jefe"), toBytes("what do ya want for nothing?"));
    test::checkBytes(Bytes(hmac2.begin(), hmac2.end()),
                     "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
                     "HMAC-SHA256 RFC4231 case 2");

    // RFC 5869 HKDF-SHA256 test case 1 (L=42).
    Bytes salt, info;
    test::check(hexDecode("000102030405060708090a0b0c", salt), "decode HKDF salt");
    test::check(hexDecode("f0f1f2f3f4f5f6f7f8f9", info), "decode HKDF info");
    const Bytes okm = hkdfSha256(Bytes(22, 0x0b), salt, info, 42);
    test::checkBytes(okm,
                     "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
                     "34007208d5b887185865",
                     "HKDF-SHA256 RFC5869 case 1");

    // Strict base64 round-trip and rejection of non-canonical input.
    const std::string cases[][2] = {
        {"", ""}, {"a", "YQ=="}, {"ab", "YWI="}, {"abc", "YWJj"},
        {"hello world", "aGVsbG8gd29ybGQ="},
    };
    for (const auto& pair : cases) {
        const Bytes input(pair[0].begin(), pair[0].end());
        test::check(base64Encode(input) == pair[1], "base64 encode: " + pair[0]);
        Bytes decoded;
        test::check(base64Decode(pair[1], decoded) && decoded == input,
                    "base64 strict decode: " + pair[1]);
    }
    Bytes ignored;
    test::check(!base64Decode("YQ", ignored), "base64 rejects missing padding");
    test::check(!base64Decode("YQ===", ignored), "base64 rejects extra padding");
    test::check(!base64Decode("YQ=A", ignored), "base64 rejects misplaced padding");
    test::check(!base64Decode("YQ== ", ignored), "base64 rejects whitespace");
    Bytes special;
    test::check(hexDecode("fbff", special) && base64Encode(special) == "+/8=",
                "base64 uses the standard +/ alphabet");
    Bytes specialDecoded;
    test::check(base64Decode("+/8=", specialDecoded) && specialDecoded == special,
                "base64 strict decode of +/ alphabet");

    // Byte helpers.
    Bytes u32, u64;
    appendU32BE(u32, 0x01020304);
    test::check(hexEncode(u32) == "01020304", "appendU32BE is big-endian");
    appendU64BE(u64, 0x0102030405060708ull);
    test::check(hexEncode(u64) == "0102030405060708", "appendU64BE is big-endian");
    test::check(constantTimeEqual(Bytes{1, 2, 3}, Bytes{1, 2, 3}), "constantTimeEqual same");
    test::check(!constantTimeEqual(Bytes{1, 2, 3}, Bytes{1, 2, 4}), "constantTimeEqual differs");
    test::check(!constantTimeEqual(Bytes{1, 2}, Bytes{1, 2, 3}), "constantTimeEqual length");

    return test::finish("crypto tests");
}
