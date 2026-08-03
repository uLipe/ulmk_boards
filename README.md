# ulmk_boards

Out-of-tree board support packages (BSPs) for [ulmk](../ulmk).  Each
subdirectory is a self-contained chip input (`ULMK_CHIP_DIR`) with `memory.ld`,
`board.cmake`, `board_config.h`, and board service sources.

| Vendor | Kit / board | SoC | Status |
|--------|-------------|-----|--------|
| Infineon | [AURIX TC275 Lite Kit](tc275_lite/) (`KIT_AURIX_TC275_LITE`) | TC275 (`SAK-TC275TP-64F200W`, TriCore, 3 cores) | Production reference — UP + SMP, full silicon cert suite |
| Witte Technology | [Linum](witte_linum/) | STM32H753ZI (Cortex-M7 @ 480 MHz) | Production reference — UP, full silicon cert suite |
| Espressif Systems | [ESP32-P4 Function EV Board](esp32p4_ev_function/) | ESP32-P4 (RV32IMAFC, CLIC + INTMTX) | **Work in progress** — boots and runs every demo, no memory isolation yet |

Every driver in every BSP follows the same contract: a server thread per
instance, reached only over IPC, with interrupts delivered as notifications
through `ulmk_irq_bind`.  `<driver>_init()` runs from the root thread and
returns the server's `ulmk_tid_t`.

## Infineon AURIX TC275 Lite Kit

Three cores, safety part.  CPU0 is TriCore **TC1.6E (1.6.1)** with 112 KB
DSPR — the 120/240 KB figures belong to CPU1/CPU2 and to TC3xx.  It is the
only board that runs the cert suite on SMP as well as UP.

| Area | Supported |
|------|-----------|
| Drivers | `port`/`pinmux`, `gpio`, `asclin`, `i2c`, `adc`, `can`, `pwm` |
| HAL | Infineon iLLD SFR headers and inlines (clone separately, see the board README) |
| Console | ASCLIN0 from userspace over USB VCOM.  No kernel printk — `ulmk_printk_char_out` is a no-op, apps call `board_console_puts()` |
| SMP | CPU0 + CPU1 + CPU2 (`--enable-smp`) |
| Demos | `board_blinky`, `board_adc_pot`, `board_pwm_led`, `board_can_loopback`, `board_i2c_scanner`, `gpio_led_notify`, `board_irq_attach_blinky` |
| SMP demos | `smp_console_3cpu`, `smp_can_demo`, `smp_tps_pwm` |
| FreeRTOS shim | `freertos_blinky`, `freertos_sem_demo` (userspace shim, not a linked RTOS) |
| Silicon cert | 14 `hil-silicon-*` suites: e2e, unit, stress, wcet, cap_neg, destroy_waiters, fault_policy, ipc_pi, irq_stress, kill_rendezvous, mem_grant, pool_exhaust, recv_or_notif_race, smp_smoke |
| Flash / debug | OpenOCD + DAS (`scripts/flash.sh`, `scripts/debug.sh`) |

## Witte Technology Linum

STM32H753ZI on a board with SDRAM and a parallel RGB panel.  Single core.

| Area | Supported |
|------|-----------|
| Drivers | `pinmux`, `gpio`, `uart`, `pwm`, `can`, `adc`, `dma`, `i2c`, `touch`, `qspi`, `display` |
| HAL | STM32 LL only — no Cube HAL objects linked |
| Memory | 2 MiB flash, AXI SRAM, 8 MiB SDRAM on FMC bank0 `0xC0000000` brought up in `board_init` |
| Display | LTDC RGB565 1024×600, double buffered in SDRAM, flip on VSYNC (tear free) |
| Touch | FT5446 on I2C3 `0x38`, INT on PH9 as an active-low EXTI |
| QSPI | W25Q128 NOR in indirect mode |
| Console | SEGGER RTT channel 0 over J-Link; USART1 remains available as a driver |
| SMP | Not applicable (single core) |
| Demos | `board_blinky`, `board_adc_scan`, `board_pwm_backlight`, `board_pwm_buzzer`, `board_can_loopback`, `board_sdram_smoke`, `board_qspi_jedec`, `board_touch_xy`, `display_hello`, `display_touch`, `lvgl_benchmark` |
| Silicon cert | 14 `hil-silicon-*` suites over RTT capture |
| Flash / debug | J-Link (`scripts/flash.sh`, `scripts/hil-rtt-capture.sh`) |

## Espressif Systems ESP32-P4 Function EV Board

RISC-V with a CLIC and an interrupt matrix ahead of it.  The newest port and
the least finished one — read the WIP note in its README before relying on it.

| Area | Supported |
|------|-----------|
| Drivers | `pinmux`, `gpio`, `uart`, `i2c`, `pwm`, `adc`, `dma`, `can`, `spi`, `dsi`, `display`, `touch` |
| HAL | ESP-IDF SOC/HAL headers plus ROM `PROVIDE` symbols — no `libidf`, no FreeRTOS linked |
| Memory | Flash XIP, HP SRAM after L2, PSRAM over AXI at `0x48000000` |
| Display | EK79007 over MIPI-DSI: video pattern generator for bring-up, then DW_GDMA streams a PSRAM framebuffer into the DPI bridge |
| ADC | ADC-Digi continuous sampling over AHB-PDMA, SAR front-end biased through REGI2C |
| CAN | TWAI0 in self-reception loopback |
| Console | UART0 written straight to the FIFO (the ROM printf is not reentrant) |
| SMP | Not supported |
| Isolation | **None yet.** PMP runs with `PRESERVE_BOOT`, which keeps an unlocked RWX window the bootloader left over the whole internal SRAM |
| Demos | `board_blinky`, `board_adc_scan`, `board_pwm_backlight`, `board_can_loopback`, `board_spi_loopback`, `board_dma_memcpy`, `display_hello`, `display_touch`, `board_pmp_neg`, `lvgl_benchmark` |
| Silicon cert | Not ported — serial capture only |
| Toolchain | `riscv32-esp-elf` from ESP-IDF on the host, not in the dev container (`ESP_IDF_PATH`) |
| Flash / debug | `esptool` (`scripts/flash.sh`), built-in USB Serial/JTAG via `scripts/openocd-p4.sh` |

## Building

From the ulmk repo:

```bash
python3 tools/dev.py build --board ../ulmk_boards/tc275_lite --component board_blinky
python3 tools/dev.py build --board ../ulmk_boards/witte_linum --component board_blinky
python3 tools/dev.py build --board ../ulmk_boards/esp32p4_ev_function --component board_blinky
```

The ESP32-P4 configures and builds natively because its toolchain lives on the
host; export `ESP_IDF_PATH` first.  The other two build inside the dev
container.

See each board's `README.md` for flash, debug and HIL steps.
