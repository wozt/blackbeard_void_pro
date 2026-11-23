#!/usr/bin/env bash
# BVP_CI_APT_APPIMAGE_V1 -- runs only on ephemeral GitHub-hosted Ubuntu CI.
set -euo pipefail
if [[ "${GITHUB_ACTIONS:-}" != true || "${RUNNER_OS:-}" != Linux ]]; then
    echo 'REFUSED: Ubuntu APT bootstrap is for GitHub Actions Linux runners only.' >&2
    exit 2
fi
if [[ "$(. /etc/os-release; echo "${ID:-}")" != ubuntu ]]; then
    echo 'REFUSED: expected Ubuntu runner.' >&2
    exit 2
fi
if [[ "$(. /etc/os-release; echo "${VERSION_CODENAME:-}")" != noble ]]; then
    echo 'REFUSED: expected Ubuntu 24.04 noble runner.' >&2
    exit 2
fi

sudo apt-get update
# On the observed Nov 23 runner, apt-get update read only Microsoft's
# repo. Check for the real package, rather than trusting update's exit code.
if ! apt-cache show libgtk-3-dev >/dev/null 2>&1; then
    echo 'Ubuntu packages absent from APT cache; adding signed official noble sources.'
    keyring=/usr/share/keyrings/ubuntu-archive-keyring.gpg
    [[ -r "$keyring" ]] || { echo "Missing Ubuntu signing keyring: $keyring" >&2; exit 1; }
    printf '%s\n' \
        'Types: deb' \
        'URIs: https://archive.ubuntu.com/ubuntu/' \
        'Suites: noble noble-updates noble-backports' \
        'Components: main restricted universe multiverse' \
        'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg' \
        '' \
        'Types: deb' \
        'URIs: https://security.ubuntu.com/ubuntu/' \
        'Suites: noble-security' \
        'Components: main restricted universe multiverse' \
        'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg' \
        | sudo tee /etc/apt/sources.list.d/blackbeard-ci-ubuntu.sources >/dev/null
    sudo apt-get update
fi
for package in build-essential libgtk-3-dev libayatana-appindicator3-dev libhidapi-dev libjson-glib-dev; do
    apt-cache show "$package" >/dev/null || { echo "Missing Ubuntu package: $package" >&2; exit 1; }
done
sudo apt-get install -y --no-install-recommends \
    build-essential pkg-config libgtk-3-dev libayatana-appindicator3-dev \
    libhidapi-dev libjson-glib-dev
