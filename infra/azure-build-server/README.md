# Azure build server

Terraform for a beefy Azure VM that takes over the combined-PCH bake (tens of
minutes, 4-6 GB on the 7.5 GB WSL2 box) and the headless engine suite.

- **Standard_E2as_v7** by default: 2 vCPU / 16 GiB, ~$0.08/hr pay-as-you-go
  (eastus), 100 GB Premium SSD. The PCH bake is one g++ process, so cores
  beyond the second sit idle; 16 GiB covers its ~6 GB peak comfortably. Override with `-var vm_size=...` /
  `-var use_spot=true` (Spot evictions deallocate; the disk survives).
- Ubuntu 24.04, provisioned by cloud-init (`user_data.sh`): apt only for
  Homebrew's prerequisites (build-essential/curl/git), then **linuxbrew** for
  the utilities (cmake/ninja/pkg-config/ccache/glm/…) and the std::embed clang
  release into `~/ctbrowser/tools/clang-std-embed`. **No SDL3** — same as CI:
  the render test and examples skip; goldens remain a local check.
- SSH only, restricted to your current public IP at apply time (or pass
  `-var ssh_cidr=...`). Auth is your `~/.ssh/id_ed25519.pub`. The public IP
  is Standard/static, so it survives deallocate/start cycles.
- **Tailscale** (optional but recommended): pass a `tailscale_auth_key` and
  cloud-init joins the server to your tailnet as `ctbrowser-build` with
  Tailscale SSH enabled — ssh works both directions (laptop→server and
  server→laptop, once the laptop runs `tailscale up --ssh` too) regardless
  of public-IP changes. `server.sh ssh` and `remote-build.sh` automatically
  prefer the tailnet address when it resolves. Generate the key at
  <https://login.tailscale.com/admin/settings/keys> and pass it via
  `TF_VAR_tailscale_auth_key` or `terraform.tfvars` (gitignored) — never
  commit it.

## Use

```bash
az login                                                  # once
export ARM_SUBSCRIPTION_ID=$(az account show --query id -o tsv)
terraform init && terraform apply                         # create/resize

./server.sh start|stop|status|ip|ssh   # stop = DEALLOCATE: compute billing
                                       # stops, the disk (and baked PCH) persists
./remote-build.sh                      # rsync working tree + submodules, run `make`
./remote-build.sh test                 # any make target(s)

terraform destroy                      # tear it all down
```

`remote-build.sh` leaves build artifacts on the server between syncs, so the
PCH bakes once and survives `stop`/`start`.

## Conventions

- `.terraform.lock.hcl` **is committed** — provider hashes only, keeps applies
  reproducible. State stays local (solo project); a commented `backend "azurerm"`
  block in `main.tf` shows how to move it to a storage account for locking/sharing.
- Variables are validated (`ssh_cidr` shape, disk minimum, name charset); all
  resources carry the same `project/component/managed_by` tags.
- The VM is SSH-key-only (`disable_password_authentication`) with Trusted
  Launch (secure boot + vTPM) enabled.

## Secrets

This directory is safe for a public repo **as long as the .gitignore holds**:
`*.tfstate*` and `*.tfvars` are ignored because state embeds your detected
home-IP CIDR (and any secrets a future config might touch). Neither the
subscription id nor credentials live in any file here — auth comes from
`az login`, and the subscription id from `ARM_SUBSCRIPTION_ID`.
