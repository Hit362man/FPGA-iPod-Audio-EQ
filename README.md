<<<<<<< HEAD
# FPGA iPod Audio Equalizer — 16-Band IIR EQ

A hardware-connected, real-time stereo audio equalizer and effects processor built in C for the **DE1-SoC FPGA development board**. The project processes samples from the board's audio peripheral, supports 16 adjustable frequency bands and optional echo/reverb, and presents a live VGA equalizer interface controlled with a PS/2 keyboard.

> **Note:** “iPod” refers to the audio-player / equalizer project. This is a bare-metal FPGA-board application, not an iOS app or an iPod firmware modification.

## Features

- **16-band stereo EQ:** a bank of cascaded fixed-point IIR low-pass sections separates each input sample into frequency bands, which are recombined using adjustable gains.
- **Real-time audio:** audio samples are read from and written to the memory-mapped audio FIFO for left and right channels.
- **Adjustable gains:** each band has a user-controlled gain setting; a nonlinear mapping provides an effective range from mute to 16×, with unity as the default.
- **Audio effects:** independent left/right echo delay lines (8,192 samples each) and three-tap reverb delay networks (601, 1,151, and 1,913 samples).
- **VGA interface:** a 320 × 240 pixel view showing live band-energy bars, 16 EQ sliders and the selected band's information.
- **PS/2 input:** keyboard-driven band selection, gain adjustment, echo/reverb toggles and reset.

## Controls

| Key | Action |
| --- | --- |
| `Z X C V B N A S D F G H J K L ;` | Select one of the 16 bands, from low to high |
| Up arrow / Down arrow | Increase / decrease gain for selected band (hold to repeat) |
| `E` | Toggle echo |
| `R` | Toggle reverb |
| Backspace | Reset all EQ gains and effects |

The VGA labels show nominal band frequencies ranging from **20 Hz to 3,000 Hz**. Actual filter response depends on the audio sampling frequency and the IIR coefficients.

## Architecture

```text
                     PS/2 keyboard
                           |
                           v
                  IIR_core.c (controls)
                           |
Audio input FIFO --> Stereo IIR EQ --> Echo / Reverb --> Audio output FIFO
                           |
                      Band analysis
                           |
                    band_visualizer.c
                           |
                       vga_eq_ui.c
                           |
                    VGA bars + sliders
```

## Source files

| File | Purpose |
| --- | --- |
| `audio_buffer.c` | Main program, stereo audio FIFO servicing and UI update loop |
| `IIR_core.c` | Fixed-point 16-band IIR audio processing, PS/2 controls, echo and reverb |
| `band_visualizer.c` | Accumulates per-band energy and smooths on-screen levels |
| `vga_eq_ui.c` | VGA pixel/character-buffer rendering and interactive sliders |
| `address_map.h` | DE1-SoC device and memory-mapped peripheral addresses |
| `Makefile` | Nios V RISC-V cross-compilation and board tooling |
| `gmake.bat` | Windows wrapper for the FPGA Academy `make.exe` |

## Building on Windows

**Prerequisites**

- DE1-SoC board with the corresponding FPGA Academy computer system loaded.
- Intel Quartus Lite / FPGA Academy AMP toolchain with `riscv32-unknown-elf-gcc` and `make.exe`.
- PS/2 keyboard, VGA monitor and suitable audio input/output connected to the board.

1. Update the `INSTALL` variable in `Makefile` if your Quartus directory differs from `C:/intelFPGA/QUARTUS_Lite_V23.1`. You can also override it when invoking make.
2. From PowerShell in the repository folder, rebuild all files:

   ```powershell
   .\gmake.bat CLEAN
   .\gmake.bat
   ```

3. A successful build produces `audio_buffer.elf`. Use the Nios V debugging / download tools provided by the FPGA Academy setup to load the executable onto the configured board.

The Makefile includes optional `DE1-SoC`, `DETECT_DEVICES`, `TERMINAL`, and GDB-related targets; these require the appropriate board configuration and hardware connection.

**Important:** The original Makefile referenced an obsolete `fft_eq.c` file. This repository builds `band_visualizer.c` instead, and `CLEAN` also removes any stale `fft_eq.o` object file.

## Verification

The C sources were checked with a host compiler to catch syntax, symbol and linker issues. **Hardware operation and the RISC-V toolchain build must still be verified on a DE1-SoC board.** The host executable must not be run: it accesses fixed hardware memory addresses.

## Future improvements

- Add a short video or photograph of the equalizer running on the DE1-SoC board.
- Record measured sampling rate, audio latency and frequency response.
- Add automated offline tests for the DSP components.
=======
# FPGA-iPod-Audio-EQ
>>>>>>> dec67f271c1c849d5b3dfbfe164cb721fc8bc78a
