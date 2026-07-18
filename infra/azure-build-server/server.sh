#!/usr/bin/env bash
# Lifecycle helper for the build server. Usage: ./server.sh {start|stop|status|ip|ssh}
# Reads VM name + resource group from terraform output in this directory.
# NOTE: "stop" DEALLOCATES — in Azure a merely-stopped VM still bills compute.
set -euo pipefail
cd "$(dirname "$0")"

vm=$(terraform output -raw vm_name)
rg=$(terraform output -raw resource_group)

current_ip() {
  terraform output -raw public_ip
}

case "${1:-status}" in
  start)
    az vm start --resource-group "$rg" --name "$vm" --output none
    echo "running at $(current_ip)"
    ;;
  stop)
    az vm deallocate --resource-group "$rg" --name "$vm" --output none
    echo "deallocated (disk persists; compute billing stops)"
    ;;
  status)
    az vm get-instance-view --resource-group "$rg" --name "$vm" \
      --query "{power: instanceView.statuses[?starts_with(code, 'PowerState/')] | [0].displayStatus, size: hardwareProfile.vmSize}" \
      --output tsv
    ;;
  ip)
    current_ip
    ;;
  ssh)
    exec ssh "ubuntu@$(current_ip)"
    ;;
  *)
    echo "usage: $0 {start|stop|status|ip|ssh}" >&2
    exit 1
    ;;
esac
