# OpenOCD + TAS — TC275 Lite Kit

Board-side configs and **aurix-openocd patch series** for the Infineon AURIX
TC275 Lite Kit (CPU0 bring-up).

## Layout

```
openocd/
  README.md                 this file
  tc275_lite.cfg            flash + debug (3-core target table)
  tc275_lite_hil.cfg        HIL UP: CPU0 only (avoids SMP poll storms)
  tc275_lite_hil_smp.cfg    HIL SMP: CPU0 + CPU1 (-defer-examine on CPU1)
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
(install script does this automatically).  Patch `0003` keeps a sole TAS
client target; multi-core still uses coreid 0/1 on that path.

## SMP / dual-core GDB

Default `tc275_lite_hil.cfg` is **CPU0-only** so HIL RAM-log peeks stay
stable.  For CPU0+CPU1:

```bash
# HIL SMP smoke already selects this via hil-silicon-smp-smoke.sh
openocd ... -f openocd/tc275_lite_hil_smp.cfg
```

CPU1 is created with `-defer-examine` (not polled until needed):

```
(gdb) monitor targets tc27x.cpu1
(gdb) monitor halt
```

CSFRs of CPU1 are also reachable from CPU0-only sessions at
`0xF883FExx` (CORE_ID / PCXI / FCX / ICR, etc.) without switching targets.
