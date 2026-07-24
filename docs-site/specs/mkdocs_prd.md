# `homemade-risc-v-64-vector-linux-emulator` MkDocs Bilingual Documentation Site PRD

## 1. Document Purpose

This document defines the MkDocs documentation site construction requirements for `homemade-risc-v-64-vector-linux-emulator`, serving as the authoritative reference for subsequent implementation, review, and acceptance of the documentation site.

The goal is to connect authoritative engineering documentation in the repository root and `specs/` into a consistently structured, bilingual, auto-deployable documentation site via relative symbolic links, while avoiding duplicating content to create multiple sources of truth.

This PRD specifically targets the current RISC-V full-system emulator project. Other repositories may reference its structure, but cannot weaken navigation, specification traceability, or symlink constraints of this project under the guise of "genericity".

## 2. Construction Objectives

Construct an independent documentation site solution satisfying the following requirements:

- Use `MkDocs` as the documentation site framework.
- Use `i18n` solutions to support bilingual Chinese and English.
- Place documentation site independently under `docs-site/` directory.
- Organize documentation entry points separately under `docs-site/docs/zh/` and `docs-site/docs/en/`.
- Chinese entry filenames use `.zh.md` suffix, while English entry filenames use `.en.md` suffix.
- Site entry Markdown must use relative symlinks pointing to authoritative documentation in the repository without copying body text.
- Maintain consistent navigation structures between Chinese and English.
- Provide explicit "简体中文 / English" language switcher at page top.
- Organize left sidebar by emulator architecture, CPU, ISA, RVV, MMU, bus, devices, VirtIO, boot, testing, and tasks.
- Provide GitHub Actions workflow for automated building and deployment.
- Overall structure should possess genericity suitable for migration to other repositories.

## 3. Applicable Scope

This PRD applies to the following project scenarios:

- Publishing project overview, disclaimers, and user guides.
- Publishing project constitution, Agent operational rules, and checkable task lists.
- Publishing RV64GCV, Sv39, MMIO, VirtIO, and Linux boot specifications.
- Supplementing module designs, maintenance guides, and verification evidence for subsequent real implementations.
- Providing corresponding documentation entry points for Chinese and English readers.

This PRD does not cover emulator code implementations, nor does it permit documentation site build flows from modifying host networks, downloading Linux images, or altering guest running states.

## 4. Overall Requirements

### 4.1 Engineering Independence

- Documentation site must exist as an independent project within `docs-site/`.
- Documentation-related configurations, pages, assets, and build logic should converge in this directory as much as possible.
- Avoid scattering documentation site implementations across unrelated repository locations.
- Minimize intrusion into main project directory structures.
- GitHub Pages workflow must reside in `.github/workflows/` due to platform constraints; this is the sole permitted site project file outside `docs-site/`.

### 4.2 Bilingual Consistency

- Support both Chinese and English simultaneously.
- Organize Chinese and English documentation in parallel structures.
- Pages covering identical topics should correspond one-to-one between Chinese and English.
- Navigation, categories, hierarchies, and naming semantics must remain consistent.
- Completing single-language structures first and patching another language temporarily is prohibited.
- Chinese documentation is current authoritative baseline; English pages must originate from real English authoritative documents, prohibiting placing Chinese symlinks into `en/` claiming completion.
- When an English topic is un-translated, keep it incomplete in tasks and clarify absence in English navigation, avoiding generating blank pages or machine placeholder text.

### 4.3 Technical Uniformity

- Documentation framework uniformly uses `MkDocs`.
- Multilingual implementations uniformly adopt `i18n` methods.
- Solutions should prefer mature, mainstream, maintainable plugins and configuration methods.
- Avoid introducing complex engineering customizations unrelated to core goals.
- Theme adopts actively maintained MkDocs Material supporting top language switching, left sidebar, search, and responsive layout.
- i18n adopts mature plugins compatible with locked MkDocs/Material versions, with dependencies strictly locked in `docs-site/requirements.lock`.

### 4.5 Fixed Directory Structure

```text
docs-site/
├── mkdocs.yml
├── requirements.lock
├── README.md
├── specs/
│   ├── mkdocs_prd.zh.md
│   └── github_action_prd.zh.md
├── docs/
│   ├── zh/                 # Stores relative symlinks pointing to Chinese authoritative documentation
│   └── en/                 # Stores relative symlinks pointing to English authoritative documentation
├── overrides/
└── assets/
```

`site/` is a local build artifact and must be ignored by Git. Resolution targets of any symlink must remain inside the repository root, prohibiting linking to user directories or other host locations.

### 4.4 Automation Requirements

- Provide standard automated build and deployment procedures.
- Automation procedures should apply to GitHub repositories.
- Support documentation build and static site deployment via GitHub Actions at minimum.
- Deployment targets prioritize compatibility with GitHub Pages.

## 5. Recommended Information Architecture Principles

Left sidebar navigation is fixed per the following information architecture:

- Home: Project positioning, disclaimers, current implementation status, and specification entries.
- Project Governance: `AGENTS.md`, project constitution, standards baseline, project tree, and task checklist.
- Overall Architecture: Product overview, module boundaries, and implementation roadmap.
- CPU & ISA: Registers, privilege modes, CSRs, scalar instructions, and Traps.
- Vector Engine: RVV 1.0 state, instructions, and exception restarts.
- Memory System: Physical bus, RAM/ROM, Sv39, TLB, and atomic A/D updates.
- Peripherals: CLINT, PLIC, UART, VirtIO common layer, block device, and network card.
- System Integration: OpenSBI, Linux, CLI, host TAP, and lifecycle.
- Quality Assurance: Testing, coding standards, artifact policy, and real acceptance.
- Site PRDs: This document and GitHub Actions PRD.

Notes:

- Navigation reflects authoritative specification semantics, rather than mechanically copying file directories.
- New modules must be added under corresponding categories without creating competing navigation systems.
- Faking uncompleted content with placeholder pages is prohibited; missing pages maintain incomplete task status.

## 6. Content Organization Requirements

### 6.1 Language Layering

- Site entry points split by language, but body text authoritative sources remain at existing documentation paths outside site directory.
- Chinese and English possess independent content entries.
- Every language possesses an independent homepage.
- Every language possesses a corresponding navigation structure.
- Markdown files under `docs-site/docs/zh/` and `docs-site/docs/en/` must be relative symlinks, prohibiting copying source files.

### 6.2 Page Naming Principles

- Page naming should be clear, stable, and predictable.
- Chinese and English pages for identical topics should maintain semantic correspondence.
- Naming should express document purpose first, avoiding arbitrary abbreviations.
- Chinese entries end with `.zh.md`, while English entries end with `.en.md`.
- Symlink name changes must update MkDocs navigation and link checks synchronously.

### 6.3 Navigation Principles

- Navigation should reflect content structures, rather than file storage locations alone.
- Homepage, getting started, user guides, specialized topics, project explanations, and PRD contents should have clear groupings.
- Navigation hierarchies should not be excessively deep.
- Chinese and English navigation structures should remain mirrored.

### 6.4 Missing Content and Expansion

- Faking completion with placeholder pages, empty translations, or un-reviewed auto-generated text is prohibited.
- When a language page is missing, it must remain unchecked in documentation site tasks in `specs/tasks.md`.
- Subsequent expansions prioritize supplementing existing categories without frequently refactoring stable URLs.

### 6.5 Symlink Integrity

- All links must use relative targets calculated from symlink directory.
- Absolute symlinks, targets resolving outside repository, circular links, and broken links are prohibited.
- When source documents are renamed, symlinks, navigation, and cross-references must update in the same change.
- Local builds and CI must validate all symlinks before launching MkDocs.

## 7. Configuration Requirements

### 7.1 MkDocs Configuration

Implementation plan must contain full MkDocs configuration, covering at least:

- Site basic information
- Repository URL and GitHub Pages `site_url`
- Theme configuration
- Navigation configuration
- Multilingual configuration
- Markdown extension configuration
- Plugin configuration
- Static asset configuration

### 7.2 i18n Configuration

Multilingual plans must satisfy the following:

- Explicitly declare supported language sets.
- Explicitly declare default language.
- Explicitly declare page mapping relationships or organization for different languages.
- Support switching between Chinese and English.
- Support expanding more languages in the future without overturning existing structures.
- Default language is Simplified Chinese, with English as secondary language; top language selector must switch between corresponding pages.

### 7.3 Readability Requirements

- Configuration files should possess clear structures and reasonable sectioning.
- Naming and comments should facilitate maintainer understanding.
- Avoid hiding core logic in hard-to-trace script assemblies.

## 8. Automation and Deployment Requirements

### 8.1 GitHub Actions

Must provide documentation site automation workflow, including at least:

- Checkout repository code
- Install build dependencies
- Build MkDocs site
- Deploy generated static files

### 8.2 Workflow Quality Requirements

- Workflows should be simple, stable, and maintainable.
- Naming should be clear for team members to understand purpose.
- Avoid significantly increasing maintenance costs for complex capabilities.
- Prefer official GitHub or community stable solutions.

### 8.3 Deployment Targets

- Prioritize targeting GitHub Pages.
- If migrating to other static hosting platforms later, structure should remain compatible.
- Deployment plans should minimize coupling with operational execution environments.

## 9. Non-Functional Requirements

### 9.1 Genericity

- Configurations should serve real information architecture of current emulator project.
- Public build logic can remain concise and portable, but must not delete project-specific navigation and specification tracking.

### 9.2 Maintainability

- New team members should quickly understand documentation project organization.
- Adding new pages later should not frequently alter core structures.
- Chinese and English content maintenance methods should be clear.

### 9.3 Consistency

- Consistent Chinese and English structures.
- Consistent page styles.
- Consistent configuration styles.
- Consistent automation process naming and responsibilities.

### 9.4 Extensibility

- Permit adding new sections in the future.
- Permit adding more languages in the future.
- Permit enhancing theme, search, SEO, versioning capabilities in the future.
- But current implementation does not require covering all advanced features at once.

## 10. Deliverables Requirements

When executing this PRD, deliver at least the following:

- `docs-site/` documentation project directory
- MkDocs main configuration file
- i18n-supporting multilingual configuration
- Chinese documentation homepage
- English documentation homepage
- Relative symlink pages pointing to existing authoritative Markdown files
- Chinese `.zh.md` and English `.en.md` naming rules
- Static assets directory
- GitHub Actions workflow file
- Brief maintenance guide

## 11. Acceptance Criteria

Satisfying the following conditions marks task completion:

1. Independent `docs-site/` documentation site project exists in repository.
2. Documentation site is built on MkDocs.
3. Documentation site supports both Chinese and English languages.
4. Multilingual implementation relies on i18n solutions, rather than manual copy-paste assembly.
5. Chinese and English homepages originate from real authoritative documents, usable as independent entries.
6. Chinese and English navigation structures remain basically consistent.
7. Site can be built automatically via GitHub Actions.
8. Site can be deployed automatically to GitHub Pages or compatible static hosting targets via GitHub Actions.
9. Directory, configuration, and content organization possess reusable value.
10. Other AIs or developers can expand based on this structure without redesigning underlying plans.
11. All site Markdown entries are relative symlinks within repository, with no copied body text, absolute links, broken targets, or out-of-bounds targets.
12. Top language switcher and left project navigation are usable in desktop and mobile layouts.

## 12. Implementation Constraints

Executors must observe the following constraints during implementation:

- Do not simplify project-specific specs into generic placeholder templates unrelated to current repository.
- Do not copy body text of `README.md`, `AGENTS.md`, or `specs/` into site directory.
- Do not omit bilingual structure designs.
- Do not complete page files while lacking automated deployment capabilities.
- Do not design directory structures relying on executor personal habits to understand.
- Do not introduce complex systems exceeding requirement scope.

## 13. Explicit Directives for Executor

Based on this PRD, design and implement a standardized documentation site solution for the target repository. Requirements:

- Use `MkDocs`
- Use `i18n` to support Chinese and English
- Place all documentation site related contents under `docs-site/`
- Connect project authoritative Markdown via relative symlinks
- Use `.zh.md` for Chinese entries and `.en.md` for English entries
- Provide top language switcher and full left project menu
- Output clear, maintainable, portable structures
- Provide basic navigation and placeholder pages
- Provide GitHub Actions auto build and deployment flows
- Prioritize compatibility with GitHub Pages
- Keep implementation simple, stable, generic

## 14. Expected Outcome

Final result is a MkDocs documentation site serving this project, supporting bilingual Chinese/English, maintaining single source of truth via symlinks, auto-deployable, and suitable for long-term maintenance.

This skeleton satisfies:

- Directly usable by current repository
- Continuously expandable in the future
- Template for other repositories
- Unified execution input for other AIs
- Minimizing result deviations among different executors
