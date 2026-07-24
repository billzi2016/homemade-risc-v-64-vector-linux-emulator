# ACT4 Conformance Testing Configuration

This directory describes the currently implemented single-hart, non-pipelined `RV64IMAFDC_Zicsr_Zifencei`
DUT. Privileged tests are explicitly disabled at this node; formal conformance testing for
M/S/U, MMU, interrupts, and RVV must extend this identical configuration after
completing corresponding production modules, and cannot be substituted by this result.

`riscv-arch-test` is fixed at ACT4 `4.0.0`, commit
`a7c99303516f4e668f7488f172043392e23b9dfd`. The executable code of this tag strictly requires Sail
RISC-V `0.10`, so this configuration follows code inspections; `0.11` mentioned in current upstream README is not used
to modify or bypass version checks of the fixed tag.

Execution entries:

```sh
tools/conformance/fetch-act4.sh
tools/conformance/run-act4.sh
```

Generated tests, ELFs, signatures, and logs reside entirely under `artifacts/`. Only when logs show all generated ELFs
are actually passed via `rvemu_conformance_runner` can `ISA-107` be marked complete.

Interrupt injection macros in `rvmodel_macros.h` are defined as NOPs in this profile, serving solely to satisfy macro completeness checks of the ACT4 common
environment header. Current configuration does not enable privileged, interrupt, CLINT, or PLIC conformance tests;
these NOPs must not serve as completion evidence for `PRV-004`, `DEV-001`, or `DEV-002`.
