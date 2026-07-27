# STM32 Cube LL + SEGGER RTT for Witte-Linum (`deps/`)

| Item | Value |
|------|--------|
| Device CMSIS | https://github.com/STMicroelectronics/cmsis_device_h7 tag `v1.10.7` |
| CMSIS Core | https://github.com/STMicroelectronics/cmsis_core tag `v5.9.0` |
| LL / HAL tree | https://github.com/STMicroelectronics/stm32h7xx_hal_driver tag `v1.11.6` |
| RTT | https://github.com/SEGGERMicro/RTT (console / printk) |
| Device | `STM32H753xx` |

**Only the LL layer** (`stm32h7xx_ll_*.h` inlines) is used. HAL `.c` / `HAL_*` are not linked.
SEGGER RTT (`SEGGER_RTT.c`) is linked for J-Link console.

## Setup

```bash
cd "$(dirname "$0")"
./fetch.sh
```

`stm32ll.cmake` + `rtt.cmake` wire includes/sources. Clock bring-up lives in
`board_init.c`. Console is RTT (not USART).
