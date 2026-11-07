# Post-build / CI size gate. Zero external dependencies: runs with `cmake -P`.
#
# Usage:
#   cmake -DORCA_TARGET_FILE=<path> [-DORCA_MAX_BYTES=<n>] -P cmake/SizeReport.cmake
#
# Defaults: exe 8 MiB, deb 10 MiB (see docs/NAMING.md).

if(NOT DEFINED ORCA_TARGET_FILE)
    message(FATAL_ERROR "[orca-size] ORCA_TARGET_FILE is required")
endif()
if(NOT EXISTS "${ORCA_TARGET_FILE}")
    message(FATAL_ERROR "[orca-size] file not found: ${ORCA_TARGET_FILE}")
endif()
if(NOT DEFINED ORCA_MAX_BYTES)
    set(ORCA_MAX_BYTES 8388608)
endif()

file(SIZE "${ORCA_TARGET_FILE}" _bytes)
math(EXPR _kib "${_bytes} / 1024")
math(EXPR _rem "(${_bytes} % 1024) * 100 / 1024")
math(EXPR _max_kib "${ORCA_MAX_BYTES} / 1024")

message(STATUS "[orca-size] ${ORCA_TARGET_FILE}")
message(STATUS "[orca-size]   size    : ${_bytes} bytes (${_kib}.${_rem} KiB)")
message(STATUS "[orca-size]   budget  : ${ORCA_MAX_BYTES} bytes (${_max_kib} KiB)")

if(_bytes GREATER ORCA_MAX_BYTES)
    message(FATAL_ERROR
        "[orca-size] SIZE GATE FAILED: ${_bytes} > ${ORCA_MAX_BYTES} bytes. "
        "See docs/DEPENDENCIES.md for the fallback (lighter WebSocket stack / dynamic OpenSSL).")
endif()
message(STATUS "[orca-size]   verdict : OK")
