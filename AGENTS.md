# AxleSense Firmware Project Instructions

## Project purpose

This repository contains firmware for the Infralytix AxleSense sensing node used for bridge monitoring and Bridge Weigh-In-Motion applications.

The current hardware is a working prototype moving toward a deployable production system for field deployment.

## Primary hardware

* MCU: STM32F411RE
* ADC: AD7124-4
* Accelerometer: ADXL354
* Communications: isolated RS-485
* Primary development environment: STM32CubeIDE / STM32CubeMX
* Firmware source is also edited using VS Code and Codex
* Logger will ultimately be a Raspberry Pi communicating with multiple nodes over RS-485

## Sensor channel configuration

### Physical AxleSense PCB wiring

The physical PCB sensor connections are:

- CH0: Accelerometer X — AIN0 to AVSS
- CH1: Accelerometer Y — AIN1 to AVSS
- CH2: Accelerometer Z — AIN2 to AVSS
- CH3: Strain — differential input using AIN4 and AIN5

The strain bridge is physically connected to the AIN4/AIN5 differential pair. Do not change this assumed hardware mapping without checking the schematic.

ADC gain, enabled channels, filter settings, and sampling configuration are firmware-configurable and should not be confused with the physical PCB wiring.

### Current known-good firmware baseline

The current committed firmware is a special strain/noise-test configuration:

- Only AD7124 channel 3 is enabled.
- Channel 3 is mapped AIN4 versus AIN5.
- Channel 3 currently uses gain = 64.
- Accelerometer channels are disabled.
- This configuration has been demonstrated working on the physical PCB.

Do not change the current ADC mapping, enabled channels, gain, filter, or setup values during structural refactoring.

The production multi-channel acquisition configuration will be deliberately implemented later.

## Current communications

Current working firmware configuration:

- The present RS-485 test output uses USART2 at 115200 baud.
- USART6 is also configured at 115200 baud in the current project but is not used for the present RS-485 output path.
- Preserve the currently working UART configuration unless explicitly instructed otherwise.
- The production communications architecture may later change as the RS-485/Modbus implementation is developed.
- Future production communications are expected to use a structured protocol such as Modbus RTU for configuration, diagnostics, and control.
- High-rate sampled data may use an efficient block-transfer mechanism over the same RS-485 connection.

Always inspect the current `.ioc` and source code before assuming which UART, pins, or baud rate are active. Do not change UART configuration without explicit approval.

## STM32CubeMX rules

This is an STM32CubeMX-generated project.

IMPORTANT:

* Do not modify generated initialization code unnecessarily.
* Never place custom application logic in regions that CubeMX may overwrite.
* Preserve all `USER CODE BEGIN` / `USER CODE END` sections.
* Prefer separate `.c` and `.h` modules for application functionality.
* Do not modify the `.ioc` file unless explicitly requested.
* Do not change MCU pin assignments, clocks, peripheral configuration, interrupt configuration, or DMA configuration without explicit approval.
* If a requested change requires CubeMX configuration, explain what needs to change rather than silently modifying generated files.

## Existing working functionality

The current committed `main` branch is a known-good hardware baseline.

The following have already been demonstrated working:

* STM32 execution and debugging
* AD7124-4 SPI communications
* AD7124 channel acquisition
* ADXL354 accelerometer acquisition through the ADC
* Strain acquisition
* Current ADC configuration
* USART output
* Physical PCB operation

Preserve this functionality while adding new features.

## Development philosophy

Make changes incrementally.

For substantial changes:

1. Inspect the existing implementation first.
2. Explain the intended change.
3. Prefer small, modular changes.
4. Avoid unnecessary rewrites of known-working code.
5. Preserve compatibility with STM32CubeIDE.
6. Keep compilation warnings and errors visible.
7. Do not alter unrelated code.
8. When uncertain about hardware behavior, ask rather than assume.

Hardware behavior observed on the physical PCB is authoritative.

## Planned firmware architecture

The firmware will gradually be organized into modules such as:

* ADC / AD7124 driver
* acquisition management
* node configuration
* calibration storage
* communications / Modbus
* diagnostics
* firmware update / bootloader interface
* timing / synchronization
* system health monitoring

`main.c` should eventually contain minimal application logic and primarily initialize hardware and run the application.

Do not perform a large architectural refactor unless explicitly requested.

## Persistent node information

The production firmware will eventually store information including:

* node serial number
* hardware revision
* firmware version
* communications address
* sampling configuration
* ADC configuration
* strain calibration values
* accelerometer calibration values
* calibration/configuration format version
* CRC or equivalent integrity check

Configuration storage and calibration storage should be treated carefully to avoid accidental corruption or unnecessary flash writes.

## Data integrity

Raw measurement data must remain available.

Do not replace raw ADC data with calibrated engineering units as the only stored representation.

Sampling systems should be designed to support:

* sample counters
* detection of dropped samples
* timing information
* buffer overrun detection
* communications error reporting

## Firmware updates

The production system is intended to support firmware updates over RS-485.

A hardware boot/reset input may also be added in a future PCB revision.

Do not implement or alter bootloader behavior without explicit discussion first.

## Git workflow

* `main` represents known-good firmware.
* Do not assume experimental changes should be committed directly to `main`.
* Prefer feature branches for meaningful new functionality.
* Keep commits focused and descriptive.
* Never remove or rewrite Git history unless explicitly instructed.

## Working style with Codex

Before making a significant change:

* inspect the relevant files
* summarize what you found
* state which files you intend to modify
* identify any hardware assumptions
* avoid changing unrelated files

When possible, build on the existing working implementation rather than replacing it.

If something is ambiguous, especially regarding STM32 pins, ADC configuration, RS-485 behavior, or physical hardware, stop and ask for clarification.
