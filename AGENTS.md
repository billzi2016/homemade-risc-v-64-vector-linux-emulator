# Agent Project Operation Rules

## 1. Scope

This document applies to all Agents, automated assistants, and maintenance sessions in this repository. Anyone reading, designing, modifying, testing, or delivering code in this project must first read this document and `specs/constitution.md`.

This project adopts Specification-Driven Development (SDD). Specifications are not suggestions; they are the sole source of truth and constraints for implementation, testing, and acceptance.

## 2. Rule Priority

When conflicts arise, handle them in the following order of priority:

1. Explicit instructions given by the user in the current session.
2. The Project Constitution in `specs/constitution.md`.
3. Confirmed specialized specifications under `specs/`.
4. Task sequence and completion conditions in `specs/tasks.md`.
5. Current implementation, historical code, and tool default behaviors.

Lower-priority content must never override higher-priority requirements. If a conflict cannot be resolved, stop operations immediately and ask the user for clarification; do not make assumptions.

## 3. Pre-Operation Confirmation

- Do not modify any files without authorization.
- Before modifying any file, explain in Chinese the purpose of the modification, the affected files, the intended changes, and expected effects, and wait for explicit user confirmation.
- When the user responds with "ok", "continue", "do it", or equivalent clear instructions to an explained and scope-bound module, it is treated as a continuous authorization for subsequent writes, real testing, task evidence logging, and agreed Git commits for that module; execute directly without requesting identical confirmation or sending empty status updates like "getting started" or "continuing" without actual actions or new information.
- `sandbox_mode = "read-only"` and `approval_policy = "on-request"` are environment constraints configured by the user; they do not constitute reasons to request redundant verbal confirmations for confirmed modules. Unless encountering genuinely new permission requests, destructive actions, specification conflicts, or missing critical dependencies, complete the current module continuously without asking "whether to continue".
- Explain the purpose of any command before executing it. Operations such as testing, building, running, generating files, installing dependencies, or downloading artifacts must receive user confirmation.
- If missing compilers, formatters, test tools, libraries, system interfaces, or other dependencies required to complete the approved module are discovered, stop immediately before installation steps. Explain in Chinese the missing items, necessity, recommended installation methods, and post-installation verification commands; let the user decide and perform installation. Installing unauthorized items or masking missing dependencies with unapproved fallback solutions, alternative tools, or skipped checks is strictly forbidden.
- When requirement details, boundaries, standard semantics, or destructive impacts are uncertain, ask the user first.
- Do not perform any Git operations unless explicitly requested by the user.
- Do not rollback, overwrite, or clean up existing changes made by the user.

## 3.1 Workspace Security Boundaries

- Restrict all reading, modifying, generating, building, and testing strictly to the repository root directory and its subdirectories by default.
- Modifying, moving, deleting, or overwriting user files, application data, or system files outside the repository is strictly forbidden.
- Modifying host software installations, environment variables, shell configurations, system services, network devices, routing, bridges, firewalls, DNS, or kernel parameters without explicit authorization is strictly forbidden.
- Commands must set the repository root directory as their working directory; do not use the user home directory, root filesystem directory, or unresolved variables as targets for recursive operations.
- When a task genuinely requires accessing resources outside the repository, standard system interfaces, or external services, parse the exact targets first, explain in Chinese the necessity, impact, and restoration procedure, and wait for explicit confirmation from the user for that operation.
- Remote operations on GitHub, network downloads, dependency installations, TAP/bridge configurations, etc., are all independent operations outside the repository boundary and cannot be presumed authorized from normal documentation or code tasks.

## 4. Specification-Driven Process

Every implementation step must follow this sequence:

1. Locate the corresponding specification and task number.
2. Verify that prerequisite tasks are completed.
3. Clarify standard references, inputs, outputs, error paths, and acceptance criteria.
4. Explain the intended modification scope to the user and wait for confirmation.
5. Use patches for minimal and complete incremental modifications.
6. Execute real tests commensurate with risk after user confirmation.
7. Record actual test results and check off tasks only when completion criteria are fully met.

If specification omissions or conflicts are discovered, update and confirm the specification first before altering the implementation. Code facts must never mask specification defects in reverse.

### 4.1 Specification Review and Error Correction

- PRDs, specialized specs, and task lists serve as implementation baselines, but they may not cover all standard details, hardware constraints, protocol boundaries, and testability issues discovered later; Agents must not use "implementing verbatim" as an excuse to ignore known specification defects.
- At the start of module design or when facing implementation bottlenecks, test conflicts, or standard ambiguities, re-verify applicable official architecture specs, protocol specs, and overall project goals to determine whether the issue stems from code, tests, or the specification itself; never default to assuming any single part is inherently correct.
- If a specification contains factual errors, internal contradictions, key omissions, unverifiable acceptance criteria, or inconsistencies with official standards, explain the issue, evidence, impact scope, and proposed spec revisions to the user first. Upon confirmation, update PRDs, specs, task dependencies, and acceptance standards via patch before continuing implementation.
- Specification revisions must improve accuracy, completeness, and testability; deleting valid requirements, narrowing necessary scopes, lowering standards, or rewriting completion definitions to accommodate existing code, reduce effort, or pass tests is strictly prohibited.
- If the original specification aligns with official standards and is feasible, but is complex, time-consuming, or currently missing foundation code, do not declare it "unreasonable"; retain the requirement and build out real implementations per dependency relationships.
- Do not silently deviate from specifications. Any confirmed specification change must retain reviewable document diffs and explain its impact on architecture, code, testing, task status, and final acceptance.

### 4.2 Complete Module Batches

- Before starting each module, list all planned additions, modifications, and deletions in a single batch, describing each file responsibility and expected outcome; explicitly state if no files are being deleted.
- Within boundaries defined by specifications and global architecture, treat modules with complete responsibilities and independent testability as implementation batches. A patch may add or modify multiple related files at once; do not artificially split a complete module into numerous piecemeal writes merely for formal incremental steps.
- Batch scope must cover production code, real tests, build wiring, and necessary documentation for that module; do not finish only easy parts while leaving key semantics as undefined placeholder stubs.
- Thoroughly inspect module dependencies, error paths, boundaries, and acceptance criteria before writing; Agent processing capacity must not justify skipping design reviews or expanding approved scopes.
- A module can only receive a Git commit after passing strict compilation, corresponding functional tests, regression tests, and applicable dynamic checks. A complete module generally corresponds to one independently reviewable and revertible commit.

## 5. Prohibition of Shortcuts and Fabrication

- Using mocks, stubs, empty implementations, fixed return values, or hardcoded outputs to fake feature completion is strictly forbidden.
- Using host system features to bypass CPU, MMU, peripheral, or protocol semantics that should be emulated is strictly forbidden.
- Declaring system goals met based solely on successful compilation, unit tests, or quick smoke tests is strictly forbidden.
- Fabricating test logs, network results, Linux boot statuses, coverage metrics, or task completion states is strictly forbidden.
- Creating multiple competing decoders, memory access paths, exception handling units, or device logic is strictly forbidden.
- Deleting, skipping, weakening, or tampering with tests—including reducing assertion strength, running only favorable subsets, swallowing failures, or altering valid expected results—to pass implementation is strictly forbidden. If a test violates official specifications, provide verifiable standard references first before correcting the test and supplementing assertions of equal or higher coverage.
- Using `TODO`, `FIXME`, empty branches, or "future implementations" to evade semantics required in the current batch is strictly forbidden. If downstream dependencies genuinely require temporary TODOs, document the corresponding task ID, blocking reason, completion condition, and cleanup node, and actively implement/remove the tag upon reaching that node.
- If test doubles must be used during transitional stages, they must be explicitly permitted by test specs, clearly scope-marked, and never replace final real-chain acceptance.
- Unexecuted checks must be explicitly marked as "unexecuted"; failed checks must be reported truthfully.

## 6. Architecture and Implementation Discipline

- Adhere to SOLID principles: single responsibility per module, explicit dependency directions, and stable interface abstractions.
- Adhere to DRY principles: core rules like register semantics, address translation, bus access, trap entry, and Virtqueue parsing must have exactly one authoritative implementation.
- The CPU must fetch instructions and access data solely through a unified memory access interface; physical accesses must be routed exclusively through a unified bus.
- Devices must not bypass the bus or controlled DMA memory interfaces to manipulate emulator state directly.
- ISA, CSR, exception codes, and device register constants must be defined centrally; magic numbers scattered in code are strictly forbidden.
- Prioritize semantic correctness, clear boundaries, and maintainability; never compromise specification coverage for development speed.

## 7. File Modification Rules

- Must use `apply_patch` for incremental changes; replacing entire files to avoid diff reviews is strictly forbidden.
- When over 90% of a file content genuinely changes and block-by-block patching loses review value, replace the content via `apply_patch` after explaining the rationale; using shell redirection, scripts, or other means to bypass patch tracking is still forbidden.
- An approved module can modify multiple related files within a single patch; using patches does not mean breaking a module into multi-round incomplete edits.
- When dependencies are clear, acceptance conditions can be jointly verified, and failures can be accurately attributed, consolidate closely related features into a single batch for implementation, building, regression testing, and committing; do not artificially fragment into frequent confirmations. Do not mix unrelated cross-layer features with hard-to-locate errors into one batch.
- Added or modified scopes must align with approved plans.
- Do not perform formatting, renaming, or adjustment on unrelated files in passing.
- Retain existing workspace modifications; if conflicts arise, pause and request user guidance.
- Project files and directory paths referenced in the repository must use relative paths from the repository root, e.g., `specs/tasks.md`; writing host user directories or workspace absolute paths is strictly forbidden.
- Documentation, configs, scripts, diagnostics, and test evidence must not depend on absolute machine locations. If referencing system device nodes (e.g., `/dev/net/tun`), explicitly mark them as host system interfaces, not repository paths.
- Temporary files and tool outputs must be placed in approved and ignored relative directories in the repository; borrowing user desktop, download directories, or system temp directories to store project artifacts is forbidden unless explicitly authorized for a specific target.

## 8. Chinese Annotation Standard

Every code file must contain:

- Chinese intent comments at the top of the file: detailing responsibilities, boundaries, key dependencies, and explicitly excluded scope.
- Chinese comments for all exported types, interfaces, functions, and non-trivial private functions: detailing design intent, parameters, return values, state changes, failure paths, exceptions, and concurrency constraints. Simple contiguous accessors with identical semantics may use a single grouped block comment, but maintainers must not be forced to guess key behaviors by reading implementation logic.
- Chinese comments at critical logic positions such as long functions, complex conditions, bitfields, privilege rules, page table walks, atomic operations, and descriptor chains.
- Explanations focusing on "why", rather than rephrasing code line-by-line.

Comment completeness and code correctness hold equal weight for completion. Omitting file intent, function duties, complex logic, and key invariant comments due to time, length, or code "seeming self-explanatory" is strictly forbidden.

Comments must stay synchronized with code; keeping inaccurate, vague, or obsolete descriptions is forbidden.

## 9. Testing and Acceptance Discipline

- Tests must cover successful paths, boundary conditions, permission rejections, invalid inputs, and state transitions.
- Behaviors for Instructions, CSRs, Sv39, VirtIO, etc., must be verified against corresponding official specifications.
- Unit tests cannot substitute for real OpenSBI, Linux, rootfs, VirtIO, and TAP system integration verification.
- Final network acceptance requires guest Linux to obtain an IP via `eth0`, perform domain name resolution, and execute ICMP ping tests over a real network link.
- When environment limitations prevent execution of acceptance tests, report the blockage truthfully; lowering acceptance standards or checking off tasks is forbidden.

## 10. External Artifacts and Git

- OpenSBI, Linux kernels, rootfs, disk images, and download caches are external/build artifacts and must never be committed to Git.
- Before downloading external artifacts, verify source, version, license, and checksum, and obtain user permission first.
- Modifying `.gitignore` is an independent implementation task and requires prior explanation and confirmation.
- When requested by the user to perform Git operations, explain every step in Chinese.
- Commit messages must use Chinese, with a clear subject and a body explaining reasons and key changes, not exceeding 10 lines total.
- Do not execute `add`, `commit`, `push`, `pull`, `merge`, `rebase`, `reset`, or `checkout` proactively.

## 11. Completion Status

A task can only be changed from `- [ ]` to `- [x]` when specifications are met, implementation is complete, real tests pass, documentation is synchronized, and no known blocking issues exist. If work is only partially completed, break down tasks or record evidence without checking the box prematurely.
