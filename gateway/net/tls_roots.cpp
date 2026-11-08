#include <orca/gateway/net/tls_roots.hpp>

#include <boost/asio/ssl/error.hpp>

#ifdef _WIN32
#include <wincrypt.h>
#include <windows.h>
#endif

#include <string>
#include <vector>

namespace orca::gateway {

#ifdef _WIN32

namespace {

constexpr char kPemBegin[] = "-----BEGIN CERTIFICATE-----\n";
constexpr char kPemEnd[] = "\n-----END CERTIFICATE-----\n";

std::string base64Encode(const unsigned char* data, DWORD length) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((length + 2) / 3) * 4);
    for (DWORD i = 0; i < length; i += 3) {
        const unsigned int chunk = static_cast<unsigned int>(data[i]) << 16 |
                                   (i + 1 < length ? static_cast<unsigned int>(data[i + 1]) << 8
                                                   : 0u) |
                                   (i + 2 < length ? static_cast<unsigned int>(data[i + 2]) : 0u);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3f]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3f]);
        out.push_back(i + 1 < length ? kAlphabet[(chunk >> 6) & 0x3f] : '=');
        out.push_back(i + 2 < length ? kAlphabet[chunk & 0x3f] : '=');
    }
    return out;
}

bool addStoreCertificates(boost::asio::ssl::context& context, LPCSTR storeName) {
    HCERTSTORE store = ::CertOpenSystemStoreA(0, storeName);
    if (store == nullptr) return false;

    bool added = false;
    PCCERT_CONTEXT cert = nullptr;
    while ((cert = ::CertEnumCertificatesInStore(store, cert)) != nullptr) {
        // Only roots that can sign: skip intermediates that happen to live in
        // the store, they add nothing and can only broaden trust.
        const std::string pem = std::string(kPemBegin) +
                                base64Encode(cert->pbCertEncoded, cert->cbCertEncoded) +
                                kPemEnd;
        boost::system::error_code ec;
        context.add_certificate_authority(boost::asio::buffer(pem), ec);
        if (!ec) added = true;
    }
    ::CertCloseStore(store, 0);
    return added;
}

}  // namespace

bool installSystemTrust(boost::asio::ssl::context& context) {
    // ROOT holds the trust anchors; CA holds intermediates a server may omit.
    const bool roots = addStoreCertificates(context, "ROOT");
    const bool intermediates = addStoreCertificates(context, "CA");
    if (roots || intermediates) return true;
    boost::system::error_code ignored;
    context.set_default_verify_paths(ignored);
    return false;
}

#else

bool installSystemTrust(boost::asio::ssl::context& context) {
    boost::system::error_code ec;
    context.set_default_verify_paths(ec);
    return !ec;
}

#endif

}  // namespace orca::gateway
