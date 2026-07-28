# board_lvgl_benchmark

LVGL **v9.5.0** (`deps/lvgl`) with a BSP-only port (`LV_OS_NONE`):

- `port_disp` — `PARTIAL` drawbufs in AXI → blit to **single** LTDC FB0
  (no flip: dual-FB+partial without front→back sync caused scene ghosting)
- `port_indev` — pointer via `touch_poll`
- `port_tick` — `board_timer_now_ticks`
- `app_benchmark` — `lv_demo_benchmark` + sysmon (FPS/mem; CPU% stub)

Further wins later: D-cache + clean before LTDC, DMA2D blit.
```bash
python3 tools/dev.py build --board ../ulmk_boards/witte_linum \
  --no-components --component board_lvgl_benchmark
```

Expect RTT: `lvgl benchmark boot` / `lvgl benchmark`. Panel shows scenes + sysmon overlay.
