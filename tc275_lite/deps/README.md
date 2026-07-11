# Infineon iLLD for TC275 Lite (`deps/`)

| Item | Value |
|------|--------|
| Upstream | https://github.com/Infineon/illd_release_tc2x |
| Tag | `V1.22.0` |
| Device tree | `src/TC27D` (SAK-TC275TP) |
| License | IFASLL (`illd_tc2x/IFASLL202501.pdf`) |

## Setup

```bash
cd "$(dirname "$0")"
git clone --depth 1 --branch V1.22.0 \
  https://github.com/Infineon/illd_release_tc2x.git illd_tc2x
cd illd_tc2x
git sparse-checkout init --cone
git sparse-checkout set \
  src/TC27D \
  examples/BaseFramework_TC27D/0_Src/AppSw/CpuGeneric/Config \
  IFASLL202501.pdf README.md
```

`illd.cmake` wires include paths into the ulmk board build.  `Ifx_Cfg.h` in
this directory sets 20 MHz XTAL / 200 MHz PLL for the Lite Kit.

## What we use today

- `IfxScuWdt_*Inline` — EndInit / password (no `.c` linked)
- `IfxScu_cfg.h` + SFR headers — PLL / FCON macros for `board_init.c`

`IfxScuCcu.c` is intentionally **not** compiled into early bring-up (`.data`
before relocation).  Userspace iLLD drivers later need mapped MMIO bases.
