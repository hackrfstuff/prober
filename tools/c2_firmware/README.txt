C2 Interface Firmware
=====================

Origin:  arduino-c2-flasher (https://github.com/nicholasgasior/arduino-c2-flasher)
Boards:  Arduino UNO / Nano (ATmega328P)
File:    uno_nano.hex

Usage:
  prober.exe --c2 --c2-port COMx --c2-install uno
  prober.exe --c2 --c2-port COMx --c2-install nano

This firmware turns an Arduino UNO or Nano into a Silicon Labs C2
debug interface, allowing direct flash/erase of EFM8 (SiLabs) ESCs
without passthrough.
