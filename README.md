# Nanopond

## Origin
This is a modified version of Nanopond version 1.9 by Adam Ierymenko.

## Build
Run the included build script, or something similar to:

$ gcc -o nanopond-ch nanopond-ch.c -lSDL

## Configuration
See the C source code for numerious compile time options.

## Files
build - simple build script
nanopond-1.9.c - original Nanopond version 1.9, Copyright (C) 2005 Adam Ierymenko.
nanopond-ch.c - my modified version
README.md - this file

## Modifications from original
- Changed to use one byte per instruction instead of packed bits in words, to more easily experiment with instructions.
- Changed instructions to 5 bit for 32 total instructions.
  - New instructions include memory/bank access, bit manipulation, arithmetic, random number, and others.
- Changed cell facing directions to allow 4 (as original), 6 (hexagonal structure) or 8 directions (N, E, S, W, and diagonals).
- Added TOTAL_ENERGY_CAP and CELL_ENERGY_CAP (optional).
- Added REPRODUCTION_COST.
- 8 bit register.
- Added Cell RAM with 4 banks: special use, private, public, and neighbor public access (facing neighbor, if allowed).
- Color schemes: "KINSHIP", "LINEAGE", "LOGO", "FACING", "ENERGY1", "ENERGY2", "RAM0", "RAM1".
- Added more stats.
