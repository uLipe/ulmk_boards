# OpenOCD + TAS — TC275 Lite Kit

Board-side configs and **aurix-openocd patch series** for the Infineon AURIX
TC275 Lite Kit (CPU0 bring-up).

## Layout

```
openocd/
  README.md                 this file
  tc275_lite.cfg            flash + debug (3-core target table)
  tc275_lite_hil.cfg        HIL: CPU0 only, reset-end WDT belt-and-suspenders
  target/infineon/tc27x.cfg chip target + pflash0 bank
  patches/                  unified diffs → upstream contribution
    series apply.sh README.md
```

Host workflow: `scripts/README-openocd.md`, `scripts/install-host-tools.sh`.

## Quick start

```bash
source ~/.local/aurix/env.sh
scripts/start-tas.sh
scripts/debug.sh          # GDB port :3333
scripts/flash.sh build/.../ulmk
```

Configs assume **go2sh/aurix-openocd** with the patches in `patches/` applied
(install script does this automatically).
