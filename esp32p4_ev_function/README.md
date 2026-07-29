# ESP32-P4 Function EV Board

> **Work in progress.** This port boots, runs every component listed below and
> passes its HIL captures, but it is not on the same footing as the other
> boards yet. Known gaps:
>
> - **No real memory isolation.** `PRESERVE_BOOT=1` keeps the bootloader's
>   entries, one of which is an unlocked RWX window over the whole internal
>   SRAM. The overlay only adds grants, so a U-mode thread still reaches
>   kernel memory. Closing this needs TOR support in the arch layer.
> - A few drivers still busy-wait where they should bind a notification
>   (`spi`); PSRAM bring-up still calls into ROM.
> - UP only, and the toolchain lives on the host rather than in the dev
>   container.
>
> Treat it as a bring-up target, not a reference BSP.

BSP `esp32p4_ev_function` — UP only. Boot: ROM → IDF bootloader @ `0x2000` →
partition @ `0x8000` → ulmk app @ `0x10000`. Toolchain: `riscv32-esp-elf`
from ESP-IDF (`ESP_IDF_PATH` / `IDF_PATH`). Drivers use HAL/LL/SOC headers +
ROM PROVIDE — **no** `libidf` / FreeRTOS link.

| Item | Value |
|------|--------|
| SoC | ESP32-P4 (RV32IMAF C, CLIC + INTMTX) |
| Flash XIP | `0x40000020` (app image) |
| SRAM | HP after L2 (`KERNEL_RAM`) |
| PSRAM | `0x48000000` AXI — probe + `psram axi ok` at boot; FB may use PSRAM |
| Console | UART bridge (often `/dev/ttyUSB1`) @ 115200 — not the JTAG ACM |
| Debug | Built-in USB Serial/JTAG (`lsusb` → `303a:1001`); OpenOCD `scripts/openocd-p4.sh` |
| Backlight / status | GPIO26 — LEDC ch0 (custom; stock EV uses GPIO23) |
| LCD reset | GPIO27 (custom jumper) |
| Display | VPG bring-up then **DW_GDMA→DPI** streams PSRAM RGB565 FB (1024×600) |
| PMP | `ULMK_ARCH_PMP_NUM=16` + `PRESERVE_BOOT=1` (BL locks early slots); LP/PSRAM/HP peri grants via `board_pmp.c` |
| IRQ | `ULMK_ARCH_HAVE_CLIC=1`, MTVT vectored, ≤32 CPU IRQs |
| HIL | `esptool` + serial capture (host + `source $IDF_PATH/export.sh`) |

## Prerequisites

```bash
export IDF_PATH=/home/ulipe/fun/esp-idf
export ESP_IDF_PATH=$IDF_PATH
source "$IDF_PATH/export.sh"
```

Prebuilt bootloader + partition table: `scripts/prebuilt/`.

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
$CAP $ELF 'SPI: PASS' 15
$CAP $ELF 'tx id=.*rx id=' 15
$CAP $ELF 'touch waiting|gt911|display touch' 15
$CAP $ELF 'psram axi ok' 15
$CAP $ELF 'PMP_NEG: PASS' 15
```

`board_lvgl_benchmark` is a **soft-FB flip stub**, not real LVGL — do not use it as a product demo.

Components: `hello_world`, `board_blinky`, `board_pwm_backlight`, `board_adc_scan`,
`board_spi_loopback`, `board_can_loopback`, `board_display_hello`,
`board_display_touch`, `board_pmp_neg` (+ stub `board_lvgl_benchmark`).
## Drivers

| Driver | Notes |
|--------|--------|
| `pinmux` | Owns IOMUX + GPIO matrix; other drivers call `pinmux_apply` |
| `gpio` | Level get/set via pinmux-configured pads |
| `pwm` | LEDC MMIO (XTAL), backlight GPIO26 via pinmux |
| `adc` | ADC-Digi continuous over AHB-PDMA; REGI2C biases the SAR front-end |
| `uart` | UART1 server (USJ console unchanged) |
| `i2c` / `touch` | I2C0 HW + GT911 X/Y |
| `spi` | **GPSPI2** full-duplex (not MSPI / not QSPI) |
| `dma` | AHB-PDMA, per-channel: memcpy plus peripheral RX (arm/wait) |
| `can` | TWAI0 self-test loopback (`tx id=` + `rx id=`) |
| `display` / `dsi` | EK79007: VPG bring-up → `dsi_fb_start` DW_GDMA→DPI FB |
| `board_lvgl_benchmark` | **Stub only** (soft FB flips) — not LVGL |
## Notes

- CLIC: save/restore `mcause` + drop `mintstatus` before preempting from tick.
- MSPI is exclusive to PSRAM/flash — do not use GPSPI for those.
- SPI GPSPI2: `trans_done` is bit 12 (not bit 0); soft loopback needs MOSI `IE`.
- PSRAM AXI: err_resp before map; config then MMU; no pre-smoke invalidate;
  HIL `psram axi ok`.
- PMP: `PRESERVE_BOOT` keeps BL slots; overlay only grants, so `board_pmp_neg`
  proves faults are caught, not that threads are isolated. See the WIP note.
- SMP out of scope for this board track.
