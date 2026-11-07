#!/usr/bin/env bash
# Install P1+ dependencies on Debian/Ubuntu (CI uses this).
set -euo pipefail

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config \
    libssl-dev libsodium-dev libboost-all-dev nlohmann-json3-dev
