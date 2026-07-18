variable "subscription_id" {
  description = "Azure subscription id. Leave null and export ARM_SUBSCRIPTION_ID instead (e.g. from `az account show --query id -o tsv`) to keep it out of any file."
  type        = string
  default     = null
}

variable "location" {
  description = "Azure region for the build server"
  type        = string
  default     = "eastus"
}

variable "vm_size" {
  description = "VM size. The combined-PCH bake needs ~4-6 GB by itself; 16 GB total keeps it off swap while the rest of the build parallelizes."
  type        = string
  # 2 vCPU / 16 GiB, ~$0.08/hr pay-as-you-go in eastus. The PCH bake is a
  # single g++ process (~6 GB peak), so one fast core + 16 GiB is the whole
  # workload; more cores only pay off if the post-bake TU count grows
  # (then D4as_v7). If capacity dries up, check:
  #  az vm list-skus --location eastus --resource-type virtualMachines -o table
  default = "Standard_E2as_v7"
}

variable "use_spot" {
  description = "Run as a Spot VM (much cheaper, but can be evicted mid-bake; evictions deallocate, the disk survives)"
  type        = bool
  default     = false
}

variable "os_disk_gb" {
  description = "OS disk size in GB (PCH + build tree are large)"
  type        = number
  default     = 100

  validation {
    condition     = var.os_disk_gb >= 30
    error_message = "os_disk_gb must be at least 30 (Ubuntu image minimum)."
  }
}

variable "ssh_public_key_path" {
  description = "Path to the SSH public key authorized on the server"
  type        = string
  default     = "~/.ssh/id_ed25519.pub"
}

variable "ssh_cidr" {
  description = "CIDR allowed to SSH in. Leave null to auto-detect your current public IP at plan time."
  type        = string
  default     = null

  validation {
    condition     = var.ssh_cidr == null || can(cidrhost(var.ssh_cidr, 0))
    error_message = "ssh_cidr must be a valid CIDR (e.g. 203.0.113.7/32)."
  }
}

variable "name" {
  description = "Name prefix for all resources"
  type        = string
  default     = "ctbrowser-build"

  validation {
    condition     = can(regex("^[a-z][a-z0-9-]{1,40}$", var.name))
    error_message = "name must be lowercase alphanumeric/hyphens, starting with a letter (it prefixes Azure resource names)."
  }
}
