# Witte-Linum (STM32H753ZI)

BSP for the **Witte Technology Linum** board (`st,stm32h753`).

DTS reference (not parsed by the build): [`dts/zephyr.dts`](dts/zephyr.dts).

| Item | Value |
|------|--------|
| SoC | STM32H753ZI (Cortex-M7 @ 480 MHz) |
| Flash | `0x08000000` (2 MiB) |
| RAM | AXI SRAM `0x24000000` |
| SDRAM | FMC bank0 @ `0xC0000000` (8 MiB), init in `board_init` |
| Display | LTDC RGB565 1024×600, double-buffer in SDRAM, flip on VSYNC |
| Touch | FT5446 @ I2C3 `0x38`, INT PH9 (active-low EXTI9) |
| QSPI NOR | W25Q128 @ QUADSPI (PF6–10 / PG6), indirect mode |
| Console | **SEGGER RTT** ch0 (J-Link); USART1 still available as driver |
| Status LED | Green LD1 = GPIOG2 (active-low) |
| HAL | **STM32 LL only** (no Cube HAL objects) |
| HIL | RTT capture (`scripts/hil-rtt-capture.sh`); silicon UP suite via RTT |

Requires kernel `ULMK_MMAP_SHARED` / `ULMK_CAP_MAP_SHARED` for SDRAM and
framebuffer windows (Normal MPU attrs — not Device).

## Setup deps

```bash
cd deps && ./fetch.sh
```

## Build

From the `ulmk` tree:

```bash
BOARD=../ulmk_boards/witte_linum
ELF=/home/ulipe/fun/build/ulipe-arm-witte_linum/ulmk

python3 tools/dev.py build --board $BOARD --clean --no-components \
  --component board_blinky
```

ELF: `build/ulipe-arm-witte_linum/ulmk`

## Flash / debug (J-Link default)

```bash
ULMK_PROBE=jlink ./scripts/flash.sh /path/to/ulmk
./scripts/debug.sh          # GDB :3333 + RTT via JLinkRTTClient
```

## HIL without UART — RTT

```bash
BOARD=../ulmk_boards/witte_linum
ELF=/home/ulipe/fun/build/ulipe-arm-witte_linum/ulmk
CAP=$BOARD/scripts/hil-rtt-capture.sh

$CAP $ELF 'Witte-Linum blinky|status LED' 15
$CAP $ELF 'SDRAM: PASS' 15
$CAP $ELF 'PWM backlight|duty' 15
$CAP $ELF 'buzzer|PWM' 15
$CAP $ELF 'CAN loopback|tx id=' 15
$CAP $ELF 'ADC scan|ch' 15
$CAP $ELF 'display hello running' 15
$CAP $ELF 'QSPI: PASS|QSPI: TC OK|QSPI JEDEC' 15
$CAP $ELF 'touch waiting|touch x=' 20   # touch panel for x/y lines
$CAP $ELF 'display touch running' 15  # banner+uptime+XY; touch for beep
```

## Drivers (client/server)

| Driver | Notes |
|--------|--------|
| `pinmux` | AF / pull / direction via LL GPIO |
| `gpio` | in/out; EXTI subscribe = ENOTSUP for now |
| `uart` | USART1 available; **console uses RTT** |
| `board_rtt` | SEGGER RTT ch0 for printk + board_console |
| `pwm` | TIM12 backlight (PH6), TIM4 buzzer; server owns TIM mmap |
| `can` | FDCAN1↔FDCAN2 loopback demo |
| `adc` | ADC one-shot via `dma` slot 0 (no local DMA mmap) |
| `dma` | DMA1+DMAMUX pool; copy-in/out IPC; worker per stream slot |
| `display` | LTDC RGB565 double-buffer in SDRAM; `display_write` / `display_flip` |
| `i2c` | I2C3 @ 400 kHz; TX/RX via `dma` slots 1/2 + EV notif |
| `touch` | FT5446 EXTI9 + I2C read (server owns EXTI/SYSCFG mmap) |
| `qspi` | QUADSPI indirect; `qspi_cmd_read` / `qspi_read`; TCF IRQ notif |

## Components (demos)

| Component | Expect on RTT |
|-----------|----------------|
| `board_blinky` | blinky / status LED |
| `board_sdram_smoke` | `SDRAM: PASS` |
| `board_pwm_backlight` | backlight PWM banner |
| `board_pwm_buzzer` | buzzer PWM banner |
| `board_can_loopback` | `tx id=` / loopback |
| `board_adc_scan` | channel scan lines |
| `display_hello` | `display hello running` + panel banner |
| `display_touch` | `display touch running`; panel XY + buzzer beep |
| `lvgl_benchmark` | `lvgl benchmark`; LVGL 9.5 DIRECT dual-FB + sysmon (FPS/mem; CPU% stub) |
| `board_qspi_jedec` | `QSPI JEDEC:` + `QSPI: PASS` (or `TC OK` if ID unexpected) |
| `board_touch_xy` | `touch probe OK` / `touch waiting` / `touch x=` |

Display banner (panel): `ulmk Microkernel` / `Hello Linum!` / `uptime: NNNNNN s`.

## Silicon certs (UP)

Same `board_services` / console / timer contract as other kits. Silicon UP
cases from `ulmk_apps/silicon/` run via the matching `scripts/hil-silicon-*.sh`
(RTT). SMP is not supported on this board.
