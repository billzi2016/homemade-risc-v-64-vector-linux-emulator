# UART 16550A & Terminal Specification

## 1. Device Positioning

UART is the sole character console for guest firmware, kernel, and Shell. Device MMIO address is `0x10000000..0x100000FF`, and its interrupt source ID must align with FDT and PLIC configuration.

## 2. Register Model

- **UART-REQ-001**: Offset 0 reads as RBR and writes as THR when DLAB=0.
- **UART-REQ-002**: Offset 5 is LSR, maintaining at least Data Ready, THR Empty, and Transmitter Empty flags correctly.
- **UART-REQ-003**: To compatibility with Linux 8250 driver, IER, IIR/FCR, LCR, MCR, DLL/DLM, and necessary behaviors probed by target drivers must be implemented.
- **UART-REQ-004**: Register widths, reset values, read/write side effects, and DLAB multiplexing must explicitly conform to 16550A compatible semantics.

Implementing only THR/RBR/LSR may be insufficient to pass Linux driver probing, so actual minimal register set must be determined by real kernel boot verification, rather than bypassing drivers via fake device tree configs.

## 3. Transmit Path

1. Guest writes THR byte.
2. UART writes byte into bounded transmit buffer or hands directly to non-blocking host terminal backend.
3. Updates THRE/TEMT only after successfully accepting byte.
4. Updates UART interrupt cause and drives PLIC interrupt line when IER allows and transmit conditions are met.

Host short writes, `EINTR`, and temporary un-writability must be handled correctly, without silently dropping characters or blocking CPU indefinitely.

## 4. Receive Path

1. Event loop performs non-blocking reads from host standard input.
2. Input bytes enter bounded receive FIFO.
3. When FIFO is non-empty, LSR.DR is set; guest reading RBR consumes one byte.
4. FIFO state changes update IIR and receive interrupt line.
5. When FIFO is full, reports overrun per deterministic policy, without overwriting unread characters silently.

## 5. Interrupt Behaviors

- IER independently controls receive data available and transmit empty interrupts.
- IIR returns current highest-priority cause and "no interrupt pending" status.
- Deasserts UART-to-PLIC level when conditions causing interrupt are cleared.
- Side effects of reading IIR, RBR, or LSR must align with register definitions.

## 6. Host Terminal Raw Mode

- **UART-REQ-005**: Switches to Raw mode only when standard input is a valid TTY; non-TTY cases adopt explicit error or pipe mode policies.
- Saves original `termios` before modification, disabling host echo, canonical line buffering, and host signal character processing.
- Control bytes such as `Ctrl+C` must pass to guest rather than default-terminating emulator.
- Must provide a controlled exit mechanism independent of terminal control characters, with specific combinations or external signals frozen in CLI spec.
- All normal exit, boot failure, host signal, and exception paths must idempotently restore original terminal attributes.

## 7. Lifecycle Safety

Terminal configuration sequence: validate all boot resources, save attributes, install restoration hooks, switch to Raw, launch machine. Destructors/cleanup must be repeatably callable. Partial modification states must not remain if switching fails.

## 8. Acceptance Criteria

- Real OpenSBI/Linux output shows no dropped bytes, out-of-order characters, or host duplicate echo.
- Shell receives normal characters, carriage return, backspace, and `Ctrl+C` character-by-character.
- UART driver works under both interrupt and polling modes.
- Host terminal attributes are restored after boot errors, normal exits, and external terminations.
- High-volume continuous output and input FIFO overflow paths undergo real stress testing.
