#!/usr/bin/env bash

set -euo pipefail

# Resolve project root (two levels up from scripts/gemrb)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/install/gemrb/build"
MANIFEST="${BUILD_DIR}/install_manifest.txt"

echo "[gemrb-uninstall] Project root: ${PROJECT_ROOT}"
echo "[gemrb-uninstall] Build dir:    ${BUILD_DIR}"

if [[ ! -d "${BUILD_DIR}" ]]; then
  echo "[gemrb-uninstall] ERROR: build directory not found: ${BUILD_DIR}" >&2
  echo "[gemrb-uninstall] Hint: run scripts/gemrb/install.sh first to create it, or remove manually." >&2
  exit 1
fi

UNINSTALL_OK=0

# 1) Try 'make uninstall' if the target exists
if [[ -f "${BUILD_DIR}/Makefile" ]] && grep -qE '^uninstall:' "${BUILD_DIR}/Makefile"; then
  echo "[gemrb-uninstall] Running 'sudo make uninstall'..."
  if sudo make -C "${BUILD_DIR}" uninstall; then
    UNINSTALL_OK=1
  else
    echo "[gemrb-uninstall] 'make uninstall' failed (continuing to manifest removal if available)."
  fi
fi

# 2) Try CMake target uninstall (some projects generate it)
if [[ ${UNINSTALL_OK} -eq 0 ]]; then
  if command -v cmake >/dev/null 2>&1; then
    echo "[gemrb-uninstall] Attempting 'sudo cmake --build . --target uninstall'..."
    if sudo cmake --build "${BUILD_DIR}" --target uninstall; then
      UNINSTALL_OK=1
    else
      echo "[gemrb-uninstall] CMake uninstall target not available or failed (continuing)."
    fi
  fi
fi

# 3) Fallback to install_manifest.txt removal
if [[ ${UNINSTALL_OK} -eq 0 ]]; then
  if [[ -f "${MANIFEST}" ]]; then
    echo "[gemrb-uninstall] Removing files from install_manifest.txt..."
    # Remove only existing files; ignore missing ones
    # xargs -r: do nothing if input is empty (portable alternative: test size first)
    if [[ -s "${MANIFEST}" ]]; then
      # Filter to existing paths to avoid noisy errors
      awk 'BEGIN{ok=0} {print $0; ok=1} END{if(ok==0) exit 1}' "${MANIFEST}" >/dev/null 2>&1 || {
        echo "[gemrb-uninstall] Manifest appears empty; skipping."; 
      }
      # Use while read to check existence then remove
      while IFS= read -r path; do
        if [[ -e "$path" ]]; then
          sudo rm -f -- "$path" || true
        fi
      done < "${MANIFEST}"
      UNINSTALL_OK=1
    else
      echo "[gemrb-uninstall] Manifest is empty; nothing to remove."
    fi
  else
    echo "[gemrb-uninstall] No install_manifest.txt found at ${MANIFEST}."
  fi
fi

# Refresh linker cache if available
if command -v ldconfig >/dev/null 2>&1; then
  echo "[gemrb-uninstall] Refreshing linker cache (sudo ldconfig)..."
  sudo ldconfig || true
fi

if [[ ${UNINSTALL_OK} -eq 1 ]]; then
  echo "[gemrb-uninstall] Uninstall finished."
  exit 0
else
  echo "[gemrb-uninstall] ERROR: Could not uninstall GemRB automatically."
  echo "[gemrb-uninstall] Try removing installed files manually or rebuild and install, then re-run this script."
  exit 1
fi


