# Giga e-paper HIL

This standalone image exercises the Waveshare SKU 26376
GDEQ0426T82/SSD1677 panel without the RS-485 HAT. It renders full black, full
white, and a bordered test pattern, then disables the HAT PWR line.

Use 3.3 V for the GH1.25 VCC connection so it matches the Giga's 3.3 V GPIO:

- D13 CLK
- D11 DIN/MOSI
- D10 CS
- D9 DC
- D8 RST
- D7 BUSY
- D6 PWR
- 3.3 V VCC
- common GND

Do not connect the display to D12. Confirm `BS=0` before powering the assembly.

```sh
pio run -d hil/giga_epaper_hil -e giga-epaper-hil -t upload
```
