# aurix-openocd patch series (TC275 Lite Kit)

Unified diffs against [go2sh/aurix-openocd](https://github.com/go2sh/aurix-openocd),
intended for upstream contribution.  Tested on **SAK-TC275TP** (KIT_AURIX_TC275_LITE)
with Infineon TAS + onboard DAP.

**Upstream baseline:** `4fbdce8` (`Add tc375 litekit.`)

## Apply

```bash
git clone --depth 1 https://github.com/go2sh/aurix-openocd.git
cd aurix-openocd
/path/to/tc275_lite/openocd/patches/apply.sh .
```

Or via board install script (applies automatically before `make`).

## Series

| Patch | File(s) | Summary |
|-------|---------|---------|
| `0001-flash-nor-tc27x-tc2xx-support.patch` | `src/flash/nor/tc3xx.c` | TC27x/TC2xx PFlash: HF_STATUS/ERRSR addresses, CBS EndInit unlock, two-phase erase/program via OCDS sequences |
| `0002-target-aurix-tc2xx-debug-step.patch` | `src/target/aurix/aurix.{c,h}` | TC2xx GDB register layout; HW breakpoints on PFlash (TRnEVT); **`aurix_step`** (TR7 trigger); DBGSR poll / running-state fixes |
| `0003-jtag-tas-client-sole-target.patch` | `src/jtag/drivers/tas_client/tas_client.c` | Single-core Lite Kit: use sole TAS target when OCDS name lookup fails |

## HIL validation (TC275)

After build + flash of `ulmk` with `hello_world`:

```bash
scripts/hil-boot-check.sh   build/.../ulmk   # HW breakpoint @ ulmk_root_thread
scripts/hil-step-test.sh      build/.../ulmk   # stepi from ulmk_kern_main → root
```

## Maintenance

Edit sources in a patched clone, then:

```bash
scripts/regen-openocd-patches.sh [/path/to/aurix-openocd]
```

Reference full files (post-patch) live in `overlays/` for review; **`.patch` files are canonical** for PRs.

## Upstream PR notes

- TriCore has **no hardware single-step**; `aurix_step` emulates one instruction via TR7 PC-match + halt (same TR_EVT as HW breakpoints).
- Instruction length: bit 0 of the first opcode byte (`0` → 16-bit, `1` → 32-bit).
- User HW breakpoints use TR slots **0–6**; slot **7** is reserved for stepping.
- Flash verify/read via TAS may still fail — leave verify off (`ULMK_FLASH_VERIFY=0`).
