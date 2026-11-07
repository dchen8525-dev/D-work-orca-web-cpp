// P3: scrypt password hashing must interoperate with the original Bun server
// (apps/server/src/passwords.ts): same encoded format, same derived bytes.
// The vectors were produced by Node's crypto.scryptSync with the canonical
// parameters, so a pass here proves a TypeScript-written hash verifies in C++.

#include <orca/crypto/scrypt.hpp>

#include "test_util.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <string>

using orca::crypto::hashPassword;
using orca::crypto::parseScryptEncoded;
using orca::crypto::verifyPassword;

namespace {

void testGoldenVectors(const nlohmann::json& vectors) {
    for (const auto& item : vectors.at("cases")) {
        const std::string password = item.at("password").get<std::string>();
        const std::string encoded = item.at("encoded").get<std::string>();
        test::check(verifyPassword(password, encoded),
                    "verifyPassword accepts the Node-derived hash for '" + password + "'");
        test::check(!verifyPassword(password + "x", encoded),
                    "verifyPassword rejects a wrong password for '" + password + "'");
        const auto params = parseScryptEncoded(encoded);
        test::check(params.has_value(), "parseScryptEncoded accepts the Node format");
        if (params) {
            test::check(params->n == 16384 && params->r == 8 && params->p == 1,
                        "Node vector uses the canonical N/r/p");
        }
    }
}

void testSelfRoundTrip() {
    const std::string encoded = hashPassword("a fresh orca-web password");
    test::check(verifyPassword("a fresh orca-web password", encoded),
                "hashPassword output verifies");
    test::check(!verifyPassword("wrong", encoded), "hashPassword output rejects wrong password");
    test::check(parseScryptEncoded(encoded).has_value(), "hashPassword output parses");
    test::check(encoded.rfind("scrypt$16384$8$1$", 0) == 0,
                "hashPassword emits the canonical header");
    // Two hashes of the same password differ (random salts) but both verify.
    const std::string second = hashPassword("a fresh orca-web password");
    test::check(second != encoded, "hashPassword salts are random");
    test::check(verifyPassword("a fresh orca-web password", second), "second hash also verifies");
}

void testMalformedInput() {
    test::check(!verifyPassword("x", ""), "empty encoded is rejected");
    test::check(!verifyPassword("x", "scrypt"), "truncated encoded is rejected");
    test::check(!verifyPassword("x", "bcrypt$16384$8$1$AAAA$AAAA"), "unknown scheme is rejected");
    test::check(!verifyPassword("x", "scrypt$16384$8$1$AAAA"), "missing hash field is rejected");
    test::check(!verifyPassword("x", "scrypt$0$8$1$AAAA$AAAA"), "N=0 is rejected");
    test::check(!verifyPassword("x", "scrypt$16384$8$1$!!!$AAAA"), "bad base64 salt is rejected");
    test::check(!parseScryptEncoded("scrypt$1$2$3$4$5$6").has_value(),
                "too many fields is rejected");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: server_passwords_test <p3-vectors.json>\n";
        return 2;
    }
    try {
        nlohmann::json vectors;
        {
            std::ifstream in(argv[1]);
            if (!in) throw std::runtime_error(std::string("cannot open ") + argv[1]);
            in >> vectors;
        }
        testGoldenVectors(vectors);
        testSelfRoundTrip();
        testMalformedInput();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: exception: " << error.what() << '\n';
        return 1;
    }
    return test::finish("server_passwords_test");
}
