#!/usr/bin/env bash
# Sync the working tree (incl. submodules) to the build server and run the build there.
# Usage: ./remote-build.sh [make target ...]     (default: the top-level `make`,
#        which bakes the combined PCH if stale, then builds + runs the headless suite)
set -euo pipefail
cd "$(dirname "$0")"

# Prefer the tailnet address (survives public-IP changes), else the public IP
vm=$(terraform output -raw vm_name)
if ! { command -v tailscale >/dev/null 2>&1 && ip=$(tailscale ip -4 "$vm" 2>/dev/null); }; then
  ip=$(./server.sh ip)
fi
if [[ -z "$ip" ]]; then
  echo "no tailnet or public ip for the server — has this been applied?" >&2
  exit 1
fi

repo_root=$(git -C .. rev-parse --show-toplevel)

# rsync the whole tree including submodule checkouts; leave remote build
# artifacts in place so the ~30 min PCH bake is reused across syncs.
# tools/clang-std-embed stays local: the server installs its own copy
# from the embed repo's GitHub release (see user_data.sh)
rsync -az --delete \
  --exclude '.git/' \
  --exclude 'build/' \
  --exclude 'tools/clang-std-embed/' \
  --exclude 'infra/azure-build-server/.terraform/' \
  --exclude '*.tfstate*' \
  --filter 'protect *.pch' --filter 'protect *.gch' --filter 'protect build/' \
  --filter 'protect tools/clang-std-embed/' \
  "$repo_root"/ "ubuntu@$ip:ctbrowser/"

ssh "ubuntu@$ip" "cd ctbrowser && make ${*:-}"
