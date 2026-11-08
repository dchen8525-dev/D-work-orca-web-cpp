#pragma once

#include <boost/asio/ssl/context.hpp>

namespace orca::gateway {

// Trust roots for outbound TLS (hub and, for wss:// endpoints, Orca).
//
// OpenSSL built by vcpkg on Windows ships no CA bundle and
// set_default_verify_paths() finds nothing useful, so a stock Windows install
// would reject every wss:// hub (certificate verify failed) even though the
// certificate chains to a Windows-trusted root. This loads the OS root store
// instead: the Windows ROOT store on Windows, OpenSSL's default paths
// elsewhere. Falls back to the default paths if the store cannot be read, so a
// caller always ends up with a context that is at least configured.
//
// Returns false only when no roots could be installed at all; the caller can
// then still use the context (it will reject unknown certificates).
bool installSystemTrust(boost::asio::ssl::context& context);

}  // namespace orca::gateway
