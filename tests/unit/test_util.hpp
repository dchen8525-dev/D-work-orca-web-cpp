#pragma once

#include <orca/crypto/bytes.hpp>

#include <iostream>
#include <string>

namespace test {

inline int& failures() {
    static int count = 0;
    return count;
}

inline void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "ok:   " << label << '\n';
    } else {
        std::cerr << "FAIL: " << label << '\n';
        ++failures();
    }
}

// Compares bytes against an expected hex string and reports the first
// differing offset. A plain "mismatch" is useless for a 1317-byte transcript.
inline bool checkBytes(const orca::crypto::Bytes& actual, const std::string& expectedHex,
                       const std::string& label) {
    orca::crypto::Bytes expected;
    if (!orca::crypto::hexDecode(expectedHex, expected)) {
        std::cerr << "FAIL: " << label << " (bad expected hex)\n";
        ++failures();
        return false;
    }
    if (actual.size() != expected.size()) {
        std::cerr << "FAIL: " << label << " (size " << actual.size() << " != " << expected.size() << ")\n";
        ++failures();
        return false;
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (actual[i] != expected[i]) {
            std::cerr << "FAIL: " << label << " (first diff at byte " << i << ": got 0x"
                      << std::hex << static_cast<int>(actual[i]) << ", want 0x"
                      << static_cast<int>(expected[i]) << std::dec << ")\n";
            ++failures();
            return false;
        }
    }
    std::cout << "ok:   " << label << " (" << actual.size() << " bytes)\n";
    return true;
}

inline int finish(const char* suite) {
    std::cout << suite << (failures() == 0 ? ": PASS\n" : ": FAIL\n");
    return failures() == 0 ? 0 : 1;
}

}  // namespace test
