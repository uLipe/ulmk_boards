# ESP32-P4 Function EV Board

BSP `esp32p4_ev_function` — dual-core (UP by default; `--enable-smp` for
CPU1). Boot: ROM → IDF bootloader @ `0x2000` → partition @ `0x8000` → ulmk
app @ `0x10000`. Toolchain: `riscv32-esp-elf` from ESP-IDF (`ESP_IDF_PATH` /
`IDF_PATH`). Drivers use HAL/LL/SOC headers + ROM PROVIDE — **no** `libidf` /
FreeRTOS link. Host build (`UL_BOARD_HOST_BUILD=1`); not in the QEMU container.

| Item | Value |
|------|--------|
| SoC | ESP32-P4 rev1 (RV32IMAFC, CLIC + INTMTX) |
| Flash XIP | `0x40000020` (app image; userspace + LVGL) |
| KERNEL_IRAM | `0x4FF20000` — 51 KiB below ROM hole; startup/trap/kernel text |
| KERNEL_RAM | `0x4FF40000` — data/BSS/stacks/pool (skips ROM SPI pointers) |
| PSRAM | `0x48000000` AXI — 8 MiB; DQS timing tune @ 200 MHz DTR |
| CPU | 400 MHz (`board_cpu_clk_set_400m`) |
| Console | UART bridge (often `/dev/ttyUSB1`) @ 115200 — not the JTAG ACM |
| Debug | Built-in USB Serial/JTAG (`lsusb` → `303a:1001`); OpenOCD `scripts/openocd-p4.sh` |
| Backlight / status | GPIO26 — LEDC ch0 (custom; stock EV uses GPIO23) |
| LCD reset | GPIO27 (custom jumper) |
| Display | EK79007: VPG bring-up then **DW_GDMA→DPI** PSRAM RGB565 (1024×600) |
| Touch | GT911 on I2C0 |
| PMP | 16 slots; unlocked roles remapped around locked boot entries; TOR user RAM |
| IRQ | `ULMK_ARCH_HAVE_CLIC=1`, MTVT vectored, ≤32 CPU IRQs |
| HIL | `esptool` + serial capture (host + `source $IDF_PATH/export.sh`) |

## Prerequisites

```bash
export IDF_PATH=/home/ulipe/fun/esp-idf
export ESP_IDF_PATH=$IDF_PATH
source "$IDF_PATH/export.sh"
```

Prebuilt bootloader + partition table: `scripts/prebuilt/`
(bootloader flash size 16 MB; factory app partition 3 MiB — see
`scripts/partitions.csv`).

LVGL for `board_lvgl_benchmark` is a board-local submodule:

```bash
git submodule update --init esp32p4_ev_function/deps/lvgl
```

## Build / flash

```bash
BOARD=../ulmk_boards/esp32p4_ev_function
ELF=/home/ulipe/fun/build/ulipe-riscv-esp32p4_ev_function/ulmk

python3 tools/dev.py build --board $BOARD --clean --no-components \
  --component hello_world

export ULMK_HIL_SERIAL=/dev/ttyUSB1
$BOARD/scripts/flash.sh $ELF
$BOARD/scripts/monitor.sh
```

## OpenOCD / JTAG (built-in)

Function EV has several Type-C ports — **only USB Serial/JTAG** is OpenOCD:

| Port (silk) | Role |
|-------------|------|
| **USB Serial/JTAG** | Built-in JTAG (`303a:1001`) + optional `/dev/ttyACM*` | ← use this |
| USB 2.0 / “USB Device” | OTG HS gadget — **not** JTAG |
| USB Full-speed | FS OTG / power |

```bash
lsusb | grep -i 303a          # must see Espressif before OpenOCD works
$BOARD/scripts/openocd-p4.sh  # -f board/esp32p4-builtin.cfg
# GDB (another terminal):
riscv32-esp-elf-gdb $ELF -ex 'target remote :3333'
```

## HIL

```bash
CAP=$BOARD/scripts/hil-serial-capture.sh
export ULMK_HIL_SERIAL=/dev/ttyUSB1

$CAP $ELF 'hello from userspace' 15
$CAP $ELF 'blinky LED off' 15
$CAP $ELF 'pwm duty=' 15
$CAP $ELF 'ch0=' 15
$CAP $ELF 'display banner on' 20
$CAP $ELF 'dsi fb stream' 15
$CAP $ELF 'SPI: PASS both instances' 15
$CAP $ELF 'tx id=.*rx id=' 15
$CAP $ELF 'touch waiting|gt911|display touch' 15
$CAP $ELF 'psram axi ok' 15
$CAP $ELF 'PMP_NEG: PASS' 15
$CAP $ELF 'GDMA_AXI: PASS' 15

# LVGL v9.5 DIRECT dual-FB benchmark (splash + scenes; needs PSRAM)
python3 tools/dev.py build --board $BOARD --clean --no-components \
  --component board_lvgl_benchmark
$CAP $ELF 'lvgl bench DONE scenes=' 220
```

Components: `hello_world`, `board_blinky`, `board_pwm_backlight`, `board_adc_scan`,
`board_spi_loopback`, `board_can_loopback`, `board_display_hello`,
`board_display_touch`, `board_pmp_neg`, `board_gdma_axi_memcpy`,
`board_lvgl_benchmark`, `smp_affinity_console`, `smp_display_touch`.

## SMP

`ULMK_ARCH_NUM_CPU=2`. With `--enable-smp` the arch calls
`ulmk_board_cpu_start()` (unstall + core1 clock + `ets_set_appcpu_boot_addr`)
and soft-IPIs via `HP_SYSTEM_CPU_INT_FROM_CPU_*` → INTMTX → CLIC IRQ 15.
QEMU RISC-V keeps the CLINT MSIP path; this board never uses CLINT.

```bash
python3 tools/dev.py build --board ../ulmk_boards/esp32p4_ev_function \
	--clean --enable-smp --no-components --component silicon_smp_smoke
bash ../ulmk_boards/esp32p4_ev_function/scripts/hil-silicon-smp-smoke.sh \
	$BUILD_DIR/ulmk
# expect: SILICON_SMP_SMOKE: PASS

python3 tools/dev.py build --board ../ulmk_boards/esp32p4_ev_function \
	--clean --enable-smp --no-components --component silicon_e2e
bash ../ulmk_boards/esp32p4_ev_function/scripts/hil-silicon-e2e.sh $BUILD_DIR/ulmk

python3 tools/dev.py build --board ../ulmk_boards/esp32p4_ev_function \
	--clean --enable-smp --no-components --component smp_affinity_console
# expect: hello on CPU0 / hello on CPU1
```

## Drivers

| Driver | Notes |
|--------|--------|
| `pinmux` | Owns IOMUX + GPIO matrix; other drivers call `pinmux_apply` |
| `gpio` | Level get/set via pinmux-configured pads |
| `pwm` | LEDC MMIO (XTAL), backlight GPIO26 via pinmux |
| `adc` | ADC-Digi continuous over AHB-PDMA; REGI2C biases the SAR front-end |
| `uart` | UART1 server (USJ console unchanged) |
| `i2c` / `touch` | I2C0 HW (P4 RSTART=6) + GT911 |
| `spi` | Multi-instance **GPSPI2/3** full-duplex over independent GDMA-AXI pairs |
| `dma` | AHB-PDMA, per-channel: memcpy plus peripheral RX (arm/wait) |
| `gdma_axi` | AXI-PDMA: memcpy plus independent GPSPI2/3 RX/TX pairs |
| `can` | TWAI0 self-test loopback (`tx id=` + `rx id=`) |
| `display` / `dsi` | EK79007 → `dsi_fb_start`; attach rearm always acks; `display_present` waits next frame |
| `board_lvgl_benchmark` | LVGL 9.5 DIRECT dual-FB in PSRAM; SW render `-Ofast`; GT911 indev |

## Notes

- CLIC: save/restore `mcause` + drop `mintstatus` before preempting from tick.
- MSPI is exclusive to PSRAM/flash — do not use GPSPI for those.
- SPI GPSPI2: `trans_done` is bit 12 (not bit 0); soft loopback needs MOSI `IE`.
- PSRAM: pad drive=2 on all pins; DQS phase/delayline tune (IDF path) then AXI
  refine; HIL expects `psram tune ok` and `psram axi ok`.
- Rev1 HP SRAM has a ROM hole (`0x4FF2CBD0`..`0x4FF40000`) holding
  `rom_spiflash_legacy_*` — do not place `.bss` there.
- Kernel/trap live in `KERNEL_IRAM` (bootloader loads the SRAM segment);
  userspace stays XIP through L1/L2.
- LVGL heap at `0x48268000` (5 MiB) after dual FBs + 64 KiB guard.
- PMP: unlocked slot map + TOR user RAM; locked boot slots 0–2/15 preserved.
