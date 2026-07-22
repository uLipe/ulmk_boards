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
| `0001-flash-nor-tc27x-tc2xx-support.patch` | `src/flash/nor/tc3xx.c` | TC27x/TC2xx PFlash: HF_STATUS/ERRSR, CBS EndInit, **256 B Write Burst (`0x7A`)**, WR64 assembly load, TAS-friendly poll backoff |
| `0002-target-aurix-tc2xx-debug-step.patch` | `src/target/aurix/aurix.{c,h}` | TC2xx GDB register layout; HW breakpoints on PFlash (TRnEVT); **`aurix_step`** (TR7 trigger); DBGSR poll / running-state fixes |
| `0003-jtag-tas-client-sole-target.patch` | `src/jtag/drivers/tas_client/tas_client.c` | Lite Kit sole-TAS-target fallback; **PL0 WR64 encode/queue** for flash assembly buffer |

## Flash performance (TC275 Lite + TAS)

| Driver | ~67 KiB image `program` phase |
|--------|-------------------------------|
| Page-only (32 B / `0xAA`) | ~39 s |
| Burst 256 B (`0x7A`) + WR64 load + poll sleep | ~15 s |

Root cause of the old “burst is unsafe” note: an earlier attempt used the
**TC3xx** Write Burst opcode (`0xA6`) on TC27x.  iLLD `IfxFlash_writeBurst`
uses **`0x7A`**.  With the correct opcode + `reset_to_read`, images boot.

Rebuild after patch changes:

```bash
# from ulmk_boards/tc275_lite
./scripts/install-host-tools.sh --skip-apt --skip-ftdi --skip-tas --skip-toolchain
# or: make -C ~/.local/aurix/build/aurix-openocd -j$(nproc) install
```

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
- TC2xx Write Burst final command word is `0x7A` (not TC3xx `0xA6`).
- Prefer `alive_sleep` between HF_STATUS polls — TAS RTT dominates busy-wait.
