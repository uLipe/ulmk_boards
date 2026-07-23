# aurix-openocd patch series (TC275 Lite Kit)

Unified diffs against [go2sh/aurix-openocd](https://github.com/go2sh/aurix-openocd).
**Upstream baseline:** `4fbdce8`

## Series

| Patch | Summary |
|-------|---------|
| `0001` | TC2xx PFlash: page + **Write Burst `0x7A`**, enter∥load split, per-chunk HF_BUSY poll, WR64 assembly load |
| `0002` | TC2xx GDB / `aurix_step` |
| `0003` | Sole-TAS-target; PL0 WR64; **one `send()` per PL0**; queue split at `pl0_max_rw` |

## Flash performance + standalone boot (TC275 Lite)

| Path | ~55 KiB program | Button / PORST |
|------|-----------------|----------------|
| Page-only (`0xAA`) | ~32–40 s | OK |
| Burst `0x7A` + enter split + poll + TAS coalesce | target ~18–25 s | Must stay OK |
| Fused enter+load / skip poll / drop `flash.sh` `reset run` | ~15 s | **Broken** |

**Release contract (`flash.sh`):** final step is OpenOCD `reset run` (clears sticky HAR / OEN). Do not replace with hot-attach-only `ulmk_release_run` without re-validating button reset.

**Firmware:** `board_wdt_early.S` before CSA is required for standalone (WDT not suspended without OCDS).
