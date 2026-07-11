# Host tools install (TC275 Lite Kit)

Automated setup for flashing and debugging on **Linux x86_64**.

## One-shot install

1. Download **TAS for Linux** from Infineon (browser; file is often named `DAS_*`):
   https://www.infineon.com/design-resources/platforms/aurix-software-tools/aurix-tools/tas  
   Example: `DAS_8.3.0_linux_x64.deb`  
   Direct link (browser):  
   https://softwaretools-hosting.infineon.com/packages/com.ifx.tb.tool.infineontoolaccesssockettas/versions/8.3.0/artifacts/DAS_8.3.0_linux_x64.deb/download

   Infineon labels the Linux TAS package `DAS_*` — legacy naming, not the Windows DAS GUI.

   The `.deb` includes **libftd2xx 1.4.27** (works on Ubuntu 22.04). You do not
   need a separate FTDI download unless you want headers for other tools.

2. (Optional) **libftd2xx** from FTDI only if not using the TAS `.deb`:
   https://ftdichip.com/drivers/d2xx-drivers/  
   Recent builds (1.4.34/1.4.35) need glibc 2.38+ — avoid on Ubuntu 22.04.

3. Run the installer:

```bash
cd ulmk_boards/tc275_lite/scripts
chmod +x install-host-tools.sh
./install-host-tools.sh \
  --tas-archive ~/Downloads/DAS_8.3.0_linux_x64.deb
```

This installs into `~/.local/aurix/` by default:

| Component | Purpose |
|-----------|---------|
| `libftd2xx` | FTDI library for onboard DAP |
| `tas/` | Infineon `tas_server` |
| `bin/openocd` | [go2sh/aurix-openocd](https://github.com/go2sh/aurix-openocd) with TAS client |
| `bin/tricore-elf-gcc` / `tricore-elf-gdb` | Host-side debug (same family as ulmk container) |
| `env.sh` | `PATH` + `LD_LIBRARY_PATH` + `ULMK_TAS_SERVER` |

Custom prefix:

```bash
./install-host-tools.sh --prefix /opt/aurix \
  --ftdi-archive /path/to/libftd2xx-linux-x86_64-*.tgz \
  --tas-archive /path/to/DAS_8.3.0_linux_x64.deb
```

Partial install / retry:

```bash
./install-host-tools.sh --skip-apt --skip-toolchain   # only OpenOCD
./install-host-tools.sh --tas-archive ... --skip-openocd  # only TAS
./install-host-tools.sh --quiet                         # hide shell commands
```

## Daily workflow

```bash
source ~/.local/aurix/env.sh

# Terminal 1 — kit on USB3
./start-tas.sh

# Terminal 2 — flash
./flash.sh /path/to/ulmk.elf

# Or debug
./debug.sh
tricore-elf-gdb -ex 'target remote :3333' /path/to/ulmk.elf
```

Serial output (userspace `board_console_puts`): USB COM port, **115200 8N1**.

## Patch series (upstream contribution)

`install-host-tools.sh` applies the unified diffs in `../openocd/patches/`
(see `openocd/patches/README.md`) on top of go2sh/aurix-openocd.

Regenerate after editing a local clone:

```bash
scripts/regen-openocd-patches.sh ~/.local/aurix/src/aurix-openocd
```

HIL smoke:

```bash
scripts/hil-boot-check.sh build/.../ulmk
scripts/hil-step-test.sh   build/.../ulmk
```

## Notes

- **TAS** may auto-download (tries `DAS_*` and `TAS_*` names); if that fails,
  download the `.deb` in a browser and pass `--tas-archive`.
- Installer writes **udev rules** and adds you to `plugdev` so `tas_server`
  runs **without sudo** (re-login or `newgrp plugdev` after first install).
- `start-tas.sh` finds `env.sh` even under `sudo` (uses `SUDO_USER`).
- OpenOCD config: `openocd/tc275_lite.cfg` + `target/infineon/tc27x.cfg`.
- `flash.sh` packs flash LOAD segments into linear Intel HEX (`scripts/elf-flash-hex.py`)
  before programming — do not pass raw ELF to OpenOCD (RAM VMAs break erase).
- **TC27x flash uses two OpenOCD sessions** (erase, then program). A single
  `program`/`erase+write` session fails after erase on hardware.
- Set `ULMK_FLASH_VERIFY=1` to attempt verify after program (currently broken
  on TAS — read path fails; leave at 0).
- Rebuild OpenOCD after pulling BSP changes:
  `./install-host-tools.sh --skip-apt --skip-ftdi --skip-tas --skip-toolchain --skip-udev`
- Build the firmware in the ulmk dev container; flash from the host with USB.

## Troubleshooting

| Symptom | Check |
|---------|--------|
| `openocd not found` | `source ~/.local/aurix/env.sh`; flash.sh uses `~/.local/aurix/bin/openocd` |
| `Can't find interface/aurix-das.cfg` | Wrong OpenOCD — need go2sh/aurix-openocd, not distro package |
| `No matching target for OCDS` | Rebuild OpenOCD after `install-host-tools.sh` (TAS Lite Kit patch); ensure `tas_server` is the only client on :24817 |
| `Failed to start session` | Another `tas_basic_client_rw` / stale OpenOCD left connected — kill it, retry |
| `Failed to execute flash erase sequence` | Rebuild OpenOCD with TC27x `tc3xx` patch (`install-host-tools.sh`); use `flash.sh` (two-phase erase+program) |
| `run after write 0xaf005554 failed` | Erase and program must be separate OpenOCD sessions — use current `flash.sh` |
| `Failed to enque read` on verify | Known TAS/read gap — flash without verify (`ULMK_FLASH_VERIFY=0`, default) |
| `gdb requested a non-existing register (reg_num=36)` | Rebuild OpenOCD with TC2xx `aurix` GDB patch (`install-host-tools.sh`) |
| `Cannot insert breakpoint` at flash | Rebuild OpenOCD with `0002` patch; `tc27x.cfg` forces `gdb breakpoint_override hard` |
| `s` / `stepi` hang in GDB | Rebuild OpenOCD — needs `0002` patch (`aurix_step`); run `hil-step-test.sh` |
| `GLIBC_2.38 not found` (host gdb) | Host Ubuntu 22.04 — use ulmk container GDB or newer distro for host toolchain |
| FTDI / USB permission | Re-run installer (udev + plugdev) |
| OpenOCD configure fails | `sudo apt install autoconf automake libtool texinfo libusb-1.0-0-dev` |

Manual details: [Infineon TAS](https://www.infineon.com/design-resources/platforms/aurix-software-tools/aurix-tools/tas),
[aurix-openocd](https://github.com/go2sh/aurix-openocd).
