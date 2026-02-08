Bluejay ESC Firmware — Bundled HEX Files
=========================================

Source: https://github.com/bird-sanctuary/bluejay/releases

Versions included:
  v0.21.0  — from tag v0.21.0
  v0.19.2  — from tag v0.19.2

File naming convention:
  {TARGET}_{PWM}_{VERSION}.hex
  e.g. A_X_5_48_v0.21.0.hex

  TARGET = layout slug with leading zeros stripped (e.g. A_X_5, A_H_120)
  PWM    = 24, 48, or 96 (kHz)

Target string normalization:
  The ESC identity target from NDJSON uses the format #A_X_05# (with leading zeros
  and surrounding hashes). To match a bundled hex file:
    1. Strip '#' characters
    2. Replace '-' with '_'
    3. Split by '_'
    4. If the last token is numeric, convert to int and back (removes leading zeros)
    5. Re-join with '_'
  Example: #A_X_05# -> A_X_5
