#!/usr/bin/env bash
# Force options needed for aximux.ko + virt boot.
set -euo pipefail
CFG="$1"
set_opt() {
  local key="$1" val="$2"
  if grep -q "^${key}=" "$CFG" || grep -q "^# ${key} is not set" "$CFG"; then
    sed -i -E "s|^# ${key} is not set|${key}=${val}|; s|^${key}=.*|${key}=${val}|" "$CFG"
  else
    echo "${key}=${val}" >> "$CFG"
  fi
}

set_opt CONFIG_MODULES y
set_opt CONFIG_MODULE_UNLOAD y
set_opt CONFIG_IKCONFIG y
set_opt CONFIG_IKCONFIG_PROC y
set_opt CONFIG_DEVTMPFS y
set_opt CONFIG_DEVTMPFS_MOUNT y
set_opt CONFIG_TMPFS y
set_opt CONFIG_PROC_FS y
set_opt CONFIG_SYSFS y
set_opt CONFIG_DEBUG_FS y
set_opt CONFIG_PINCTRL y
set_opt CONFIG_OF y
set_opt CONFIG_OF_OVERLAY y
set_opt CONFIG_BLK_DEV_INITRD y
set_opt CONFIG_RD_GZIP y
set_opt CONFIG_SERIAL_AMBA_PL011 y
set_opt CONFIG_SERIAL_AMBA_PL011_CONSOLE y
set_opt CONFIG_VIRTIO_MENU y
set_opt CONFIG_VIRTIO_MMIO y
set_opt CONFIG_BINFMT_ELF y
set_opt CONFIG_BINFMT_SCRIPT y
# Soft float vs hard: poky is hf; multi_v7 usually AEABI
set_opt CONFIG_AEABI y
