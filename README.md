# ulmk_boards

Out-of-tree board support packages (BSPs) for [ulmk](../ulmk).  Each
subdirectory is a self-contained chip input (`ULMK_CHIP_DIR`) with `memory.ld`,
`board.cmake`, `board_config.h`, and board service sources.

| Board | Kit / SoC | Status |
|-------|-----------|--------|
| [tc275_lite](tc275_lite/) | Infineon AURIX TC275 Lite Kit (`KIT_AURIX_TC275_LITE`) | Phase 0+1 (clock, STM0, ASCLIN0 console) |

Build from the ulmk repo:

```bash
python3 tools/dev.py build --board ../ulmk_boards/tc275_lite --component hello_world
```

See each board's `README.md` for flash/debug steps.
