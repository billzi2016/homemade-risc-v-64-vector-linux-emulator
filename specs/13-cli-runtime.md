# CLI & Runtime Lifecycle Specification

## 1. Command Format

macOS or non-networked boot:

```text
./riscv_vector_emulator \
  --bios artifacts/firmware/opensbi.bin \
  --kernel artifacts/kernel/Image \
  --disk artifacts/disk/rootfs.ext4 \
  --net none
```

Linux TAP network boot:

```text
./riscv_vector_emulator \
  --bios artifacts/firmware/opensbi.bin \
  --kernel artifacts/kernel/Image \
  --disk artifacts/disk/rootfs.ext4 \
  --net tap0
```

Repository file argument examples must use relative paths from repository root. Program accepts user explicitly passed alternative paths, but must not write current development machine absolute paths into default values, log templates, or configurations.

## 2. Mandatory Arguments

- **RUN-REQ-001**: `--bios` specifies firmware.
- **RUN-REQ-002**: `--kernel` specifies kernel.
- **RUN-REQ-003**: `--disk` specifies rootfs image.
- **RUN-REQ-004**: `--net` is an optional network selection; `none` disables networking, while other non-empty values are interpreted strictly as TAP interface names on Linux. Omitting this argument is equivalent to `--net none`.

Duplicate arguments, unknown arguments, missing values, and empty values must report errors. Program must not disguise arbitrary strings as available TAPs on macOS, nor create network backends secretly when network options are omitted.

## 3. Startup Validation Sequence

1. Parse arguments, producing no external side effects.
2. Validate file readability/writability policies, formats, sizes, and mutual compatibility.
3. Validate RAM configuration and load layout.
4. Open and validate disk; open and validate Linux TAP only when network mode is not `none`.
5. Construct machine, load images, and FDT.
6. Install resource cleanup hooks.
7. Switch terminal to Raw mode as the final step.
8. Enter execution loop.

Any early failure must report while the terminal remains in normal mode.

## 4. Sole Main Loop

- **RUN-REQ-005**: The entire project must maintain exactly one production execution loop.
- Check interrupts, fetch, decode, execute, and commit Traps at instruction boundaries.
- Advance CLINT and service UART/TAP/virtqueues per explicit policies.
- Use non-blocking event handling, disallowing a single host FD from permanently blocking CPU; non-networked profile creates no fake network FDs or polling branches.
- Support WFI: block-wait when no events exist, waking upon event arrival or timer expiration.
- Skipping instructions, faking device completions, or invoking host commands directly to accelerate acceptance is prohibited.

## 5. Exits and Signals

In Raw mode, `Ctrl+C` is handed to guest. Host requests exit via documented escape sequences, second-terminal signals, or other explicit mechanisms. Exit sequence must:

1. Stop accepting new device work.
2. Complete or safely cancel host asynchronous operations.
3. Deassert device interrupts and close disk and actually opened TAP FDs.
4. Restore terminal attributes.
5. Output necessary diagnostics and return stable exit code.

Signal handlers themselves perform only async-signal-safe operations, notifying main loop cleanup via flags.

## 6. Exit Codes

Distinguish at least: successful exit, CLI usage error, resource/permission error, image format error, runtime host I/O error, and internal emulator error. Whether guest shutdown constitutes success must be explicit; guest normal Traps should not map to host error exits.

## 7. Logging and Diagnostics

- Default UART bytes passthrough standard output, diagnostics use standard error, avoiding polluting guest console streams.
- Log levels and categories are explicit, defaulting to not printing per-instruction traces.
- Optional trace files are placed in ignored relative directories such as `artifacts/logs/`.
- Diagnostics contain guest PC, privilege mode, access type, or device name, but must not leak host-unrelated data.
- Logs cannot replace device side effects.

## 8. Acceptance Criteria

- macOS non-networked command and Linux TAP command both launch in a single pass without popping up GUIs or interactive prompts.
- All argument errors report clearly before producing external state.
- CPU and timers continue advancing under sustained UART and network activity.
- Terminal is restored after normal exits, resource failures, and signal exits.
- Sole main loop is covered by real system boot tests.
