output "vm_name" {
  description = "VM name, consumed by server.sh"
  value       = azurerm_linux_virtual_machine.build.name
}

output "resource_group" {
  description = "Resource group name, consumed by server.sh"
  value       = azurerm_resource_group.build.name
}

output "public_ip" {
  description = "Static — survives deallocate/start cycles"
  value       = azurerm_public_ip.build.ip_address
}

output "ssh" {
  description = "Ready-to-paste SSH command"
  value       = "ssh ubuntu@${azurerm_public_ip.build.ip_address}"
}
