terraform {
  required_version = ">= 1.5"
  required_providers {
    azurerm = {
      source  = "hashicorp/azurerm"
      version = "~> 4.0"
    }
    http = {
      source  = "hashicorp/http"
      version = "~> 3.4"
    }
  }
}

# State is local by default — fine for one person, and it's gitignored because
# it embeds your detected home-IP CIDR. For shared state + locking, create a
# storage account once, uncomment this block, and run:
#   terraform init -backend-config=backend.hcl
# where backend.hcl (kept local, gitignored) sets resource_group_name,
# storage_account_name, container_name and key.
# terraform {
#   backend "azurerm" {}
# }

provider "azurerm" {
  features {
    # Azure auto-injects a VM-Insights data collection rule (msvmi-*) into the
    # RG; without this flag `terraform destroy` refuses to delete the RG.
    resource_group {
      prevent_deletion_if_contains_resources = false
    }
  }
  # Falls back to ARM_SUBSCRIPTION_ID when the variable is unset
  subscription_id = var.subscription_id
}

locals {
  tags = {
    project    = "compile-time-browser"
    component  = "build-server"
    managed_by = "terraform"
  }
}

# Caller's public IP, used when ssh_cidr isn't set explicitly
data "http" "my_ip" {
  url = "https://checkip.amazonaws.com"
}

locals {
  ssh_cidr = coalesce(var.ssh_cidr, "${chomp(data.http.my_ip.response_body)}/32")
}

resource "azurerm_resource_group" "build" {
  name     = "${var.name}-rg"
  location = var.location
  tags     = local.tags
}

resource "azurerm_virtual_network" "build" {
  name                = "${var.name}-vnet"
  resource_group_name = azurerm_resource_group.build.name
  location            = azurerm_resource_group.build.location
  address_space       = ["10.42.0.0/24"]
  tags                = local.tags
}

resource "azurerm_subnet" "build" {
  name                 = "${var.name}-subnet"
  resource_group_name  = azurerm_resource_group.build.name
  virtual_network_name = azurerm_virtual_network.build.name
  address_prefixes     = ["10.42.0.0/26"]
}

resource "azurerm_network_security_group" "build" {
  name                = "${var.name}-nsg"
  resource_group_name = azurerm_resource_group.build.name
  location            = azurerm_resource_group.build.location

  security_rule {
    name                       = "ssh"
    priority                   = 100
    direction                  = "Inbound"
    access                     = "Allow"
    protocol                   = "Tcp"
    source_port_range          = "*"
    destination_port_range     = "22"
    source_address_prefix      = local.ssh_cidr
    destination_address_prefix = "*"
  }

  tags = local.tags
}

# Standard SKU = static: the IP survives deallocate/start cycles
resource "azurerm_public_ip" "build" {
  name                = "${var.name}-ip"
  resource_group_name = azurerm_resource_group.build.name
  location            = azurerm_resource_group.build.location
  allocation_method   = "Static"
  sku                 = "Standard"
  tags                = local.tags
}

resource "azurerm_network_interface" "build" {
  name                = "${var.name}-nic"
  resource_group_name = azurerm_resource_group.build.name
  location            = azurerm_resource_group.build.location

  ip_configuration {
    name                          = "primary"
    subnet_id                     = azurerm_subnet.build.id
    private_ip_address_allocation = "Dynamic"
    public_ip_address_id          = azurerm_public_ip.build.id
  }

  tags = local.tags
}

resource "azurerm_network_interface_security_group_association" "build" {
  network_interface_id      = azurerm_network_interface.build.id
  network_security_group_id = azurerm_network_security_group.build.id
}

resource "azurerm_linux_virtual_machine" "build" {
  name                  = var.name
  resource_group_name   = azurerm_resource_group.build.name
  location              = azurerm_resource_group.build.location
  size                  = var.vm_size
  admin_username        = "ubuntu"
  network_interface_ids = [azurerm_network_interface.build.id]

  priority        = var.use_spot ? "Spot" : "Regular"
  eviction_policy = var.use_spot ? "Deallocate" : null

  # SSH-key only; explicit even though it's the provider default
  disable_password_authentication = true

  # Trusted Launch (supported by Fsv2 + the Gen2 Ubuntu 24.04 image)
  secure_boot_enabled = true
  vtpm_enabled        = true

  admin_ssh_key {
    username   = "ubuntu"
    public_key = file(pathexpand(var.ssh_public_key_path))
  }

  os_disk {
    caching              = "ReadWrite"
    storage_account_type = "Premium_LRS"
    disk_size_gb         = var.os_disk_gb
  }

  source_image_reference {
    publisher = "Canonical"
    offer     = "ubuntu-24_04-lts"
    sku       = "server"
    version   = "latest"
  }

  custom_data = base64encode(templatefile("${path.module}/user_data.sh", {
    tailscale_auth_key = var.tailscale_auth_key
    tailscale_hostname = var.name
  }))

  tags = local.tags
}
