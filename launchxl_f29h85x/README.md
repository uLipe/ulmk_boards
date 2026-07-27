# LAUNCHXL-F29H85X BSP

TI F29H850TU9 launchpad board support for the ULMK C29 port.

## Requirements

- TI CCS 20.4+ with C29 Clang (`TI_C29_CGT_ROOT` / `TI_CCS_ROOT`)
- XDS110 probe serial `CL850001` (override with `ULMK_HIL_PROBE_SERIAL`)
- Application UART: XDS110 `if00` (`/dev/serial/by-id/...CL850001-if00`)

## Build (RAM / CPU1)

```bash
export PATH=/home/ulipe/ti/ccs2040/ccs/tools/compiler/ti-cgt-c29_2.0.0.STS/bin:$PATH
export TI_C29_CGT_ROOT=/home/ulipe/ti/ccs2040/ccs/tools/compiler/ti-cgt-c29_2.0.0.STS

cmake -S /home/ulipe/fun/ulmk -B /home/ulipe/fun/build/ulipe-c29-launchxl_f29h85x \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-c29-ticlang.cmake \
  -DULMK_CHIP_DIR=/home/ulipe/fun/ulmk_boards/launchxl_f29h85x \
  -DULMK_COMP_c29_banner_ENABLED=ON \
  -DULMK_CONFIG_ENABLE_SMP=0 -GNinja

ninja -C /home/ulipe/fun/build/ulipe-c29-launchxl_f29h85x
cp /home/ulipe/fun/build/ulipe-c29-launchxl_f29h85x/ulmk \
   /home/ulipe/fun/build/ulipe-c29-launchxl_f29h85x/ulmk.out
```

## HIL

```bash
# Banner / Gate B sleep
./scripts/hil-load-uart.sh /path/to/ulmk.out 'C29SLEEP_PASS' 55

# Blinky (UART + visual LED4/LED5 alternating ~100 ms)
./scripts/hil-blinky.sh /path/to/ulmk
# Visual checklist: LED4 and LED5 alternate; neither stuck on/off.
```

Flash/SSUMODE2 requires `ULMK_C29_FLASH=1` plus reviewed
`ULMK_C29_BANKMODE` / `ULMK_C29_LIFECYCLE` — the script refuses unknown
profiles.  SECCFG packaging: `scripts/package-seccfg.sh`.

### Reviewed flash profile (Gate G)

| Variable | Accepted value | Notes |
|----------|----------------|-------|
| `ULMK_C29_BANKMODE` | `0` | Dual-bank MODE0 for UP bring-up |
| `ULMK_C29_LIFECYCLE` | `HSFS` | No COMMIT / MODE3 in this package |
| `ULMK_C29_SECCFG_COMMIT` | unset / `0` | Default: strip NonMain SECCFG from flashable |
| `ULMK_C29_SECCFG_COMMIT` | `1` | **Dangerous** — erase/program NonMain SECCFG for MODE2 (`hil-ssu-mode2.sh`) |

Regenerate MODE2 blobs (host):

```bash
python3 tools/c29_seccfg_gen.py --sdk "$TI_F29_SDK_ROOT" \
  --objcopy "$TI_C29_CGT_ROOT/bin/c29objcopy" \
  --out-dir ../ulmk_boards/launchxl_f29h85x/seccfg --check
```

Recovery before NonMain erase: `xds110reset` + RAM reload via `hil-load-uart.sh`.

## SMP

`-DULMK_CONFIG_ENABLE_SMP=1` (board declares `ULMK_BOARD_C29_DLB_WORKAROUND=1`).
CPU2/CPU3 stubs live at LPA1 `0x20108000` and CPA0 `0x20110000` inside the
single `ulmk.out` artifact.

## Errata

`SPRZ569E` — clear all three `MEM_DLB_CONFIG` enable bits before shared RAM
use / secondary release (done in `ulmk_arch_init`).
