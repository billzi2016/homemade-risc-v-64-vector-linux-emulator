# `homemade-risc-v-64-vector-linux-emulator` Documentation Site GitHub Actions Deployment PRD

## 1. Document Purpose

This document defines the automated check, build, and GitHub Pages deployment requirements for the current emulator repository documentation site using MkDocs.

The goal is to strictly validate the bilingual symlink documentation tree without touching emulator builds, Linux artifacts, or host networking, and to deploy a reproducible static site to this repository's GitHub Pages.

This PRD is bound to `billzi2016/homemade-risc-v-64-vector-linux-emulator`. The workflow can remain clean and portable, but trigger paths, site directories, and Pages targets must adhere to this project.

## 2. Project Goals

An independent, standard, and maintainable GitHub Actions workflow needs to be provided for the target repository to fulfill the following goals:

- Support automated build and deployment execution on GitHub.
- Support automated publishing of static documentation sites.
- Deploy to the GitHub Pages of this public repository.
- Validate Chinese/English entry naming, navigation completeness, and relative symlink safety prior to building.
- Build `docs-site/mkdocs.yml` using locked dependencies in `docs-site/requirements.lock`.
- Maintain a clean workflow structure that is easy to understand, maintain, and migrate.
- Ensure the solution is general enough to serve as a reference template for other repositories.

## 3. Scope

This PRD covers only the following scenarios:

- Documentation builds after changes to `README.md`, `AGENTS.md`, `specs/**`, or `docs-site/**`.
- Strict build validation for documentation PRs.
- GitHub Pages deployment after documentation changes on the `main` branch.
- Manual `workflow_dispatch` rebuilds and re-deployments.

This PRD does not run the emulator, download OpenSBI/Linux/rootfs, create TAPs, execute project releases, nor use documentation deployment credentials for code or release writes.

## 4. General Requirements

### 4.1 Independence

- The GitHub Actions deployment solution should be independently defined.
- Automated workflows should focus on documentation site building and deployment, without mixing in unrelated business logic.
- Workflow responsibilities should be single and clear, avoiding a single file carrying excessive unrelated tasks.
- The workflow file is fixed at `.github/workflows/docs-pages.yml`; except for platform-required files, configs, dependencies, and validation scripts are consolidated under `docs-site/`.

### 4.2 Portability

- The workflow must explicitly use `docs-site/`, project authoritative Markdown files, and the GitHub Pages environment.
- Common steps may be reused, but portability must not be used as an excuse for broad `push` triggers or skipping project symlink checks.

### 4.3 Stability

- Prefer stable official or community GitHub Actions.
- Minimize unnecessary custom scripts.
- The workflow should be runnable under standard repository permission models.

## 5. Workflow Responsibilities

Automated workflows must cover at least the following responsibilities:

- Trigger automatically upon code changes.
- Support manual triggers.
- Checkout repository code.
- Prepare execution environment.
- Install documentation build dependencies.
- Validate that all documentation symlinks are relative links, targets exist, and resolved paths remain within the repository.
- Validate that `docs-site/docs/zh/` contains only `.zh.md` Markdown entries, and `docs-site/docs/en/` contains only `.en.md` Markdown entries.
- Execute documentation site build.
- Upload build artifacts.
- Deploy static site to target hosting platform.

## 6. Trigger Strategy Requirements

### 6.1 Automated Triggers

- Workflow should support automatic execution after main branch updates.
- Should not trigger by default on all `push` events on the main branch.
- Must restrict triggers to documentation-related content changes to minimize wasteful runs.
- Should use `paths` or equivalent mechanisms to restrict triggers to documentation source code, configs, deployment workflows, and designated entry files.
- Trigger path design should reflect clean documentation engineering boundaries.
- Automatic deployment trigger paths are fixed to include `README.md`, `AGENTS.md`, `specs/**`, `docs-site/**`, and `.github/workflows/docs-pages.yml`.
- Pull Requests can run validation builds, but must not deploy Pages; only controlled pushes to `main` or manual triggers can deploy.

### 6.2 Manual Triggers

- Workflow should support `workflow_dispatch`.
- Manual triggers are used for debugging, re-deployments, and manual verification.

### 6.3 Concurrency Control

- Concurrency control should be implemented to prevent redundant deployments from overwriting each other.
- Concurrency groups should ensure predictable behavior and clear semantics.
- Pages deployment uses a single concurrency group; new commits can cancel pending builds, but must not abort deployments that have entered non-cancellable release stages.

## 7. Build Requirements

### 7.1 Execution Environment

- Use stable, mainstream runner OS versions.
- Environment preparation steps should be clean and readable.
- Do not introduce significant complexity for minor optimizations.
- Use GitHub-hosted stable Linux runners to ensure relative symlinks are checked out and resolved according to Linux semantics.

### 7.2 Dependency Installation

- Dependency installation methods should be explicit and stable.
- Use the documentation project's own dependency lockfile, rather than relying on host implicit environments.
- Dependency sources and steps should be maintainer-friendly.
- Python, MkDocs, Material theme, and i18n plugin versions must be locked; do not implicitly install uncontrolled latest versions on each CI run.

### 7.3 Build Execution

- Build commands should be direct, clear, and reproducible.
- Build failures must clearly expose issues rather than skipping silently.
- Enable strict mode if supported by build tools to catch documentation defects early.
- Fixed build entry point: execute `mkdocs build --strict --config-file docs-site/mkdocs.yml` from the repository root.
- Run project symlink/navigation checks before building; failed checks must not proceed to artifact upload.

## 8. Deployment Requirements

### 8.1 Target Platform

- Statically support GitHub Pages for current repository.
- Ensure the overall workflow is easily portable if switching to other static hosting platforms in the future.

### 8.2 Artifact Handling

- Separate build artifacts from source responsibilities.
- Upload and publish artifacts using standardized steps.
- Do not tightly couple deployment logic with documentation source organization.
- Upload only MkDocs-generated static `site/` directory; do not upload source code, build caches, Linux images, or other repository files.

### 8.3 Permission Requirements

- Workflow permissions should follow the principle of least privilege.
- Grant only permissions necessary for build and deployment.
- Do not configure high-privilege capabilities exceeding documentation deployment needs.
- Build uses `contents: read`; deployment adds only `pages: write` and `id-token: write`. Do not grant `contents: write`, `packages: write`, or repository admin rights.
- Pages deployment job must bind to the GitHub `github-pages` environment and expose final site URL.

### 8.4 Action Supply Chain Constraints

- Prefer official GitHub checkout, Pages configuration, artifact upload, and Pages deployment Actions.
- Actions must lock reviewed stable major versions or full commit SHAs; version upgrades are independent changes requiring verification.
- Writing to repository or deploying from unreviewed third-party Actions is strictly prohibited.

## 9. Maintainability Requirements

### 9.1 Readability

- Workflow files should have clear naming.
- Job and Step names should have explicit semantics.
- Maintainers should quickly understand each step's responsibility.

### 9.2 Portability

- Workflows should clearly reflect current repository documentation boundaries; explicit modifications to repo name, trigger paths, and navigation checks are required when migrating.

### 9.3 Extensibility

- Allow future additions of pre-checks, link checking, formatting checks, or multi-version deployment capabilities.
- Do not stack complex CI/CD features in a single pass.

## 10. Non-Functional Requirements

### 10.1 Simplicity

- Solutions should be direct and simple.
- Do not introduce excessive unrelated steps just to look complete.

### 10.2 Consistency

- Workflow naming, step organization, and commenting styles should be uniform.
- Automation solutions should match the documentation engineering structure.

### 10.3 Reliability

- Maintain stable performance for common documentation release scenarios.
- Avoid fragile temporary scripts or implicit dependencies.

## 11. Deliverable Requirements

When implementing this PRD, deliver at least:

- GitHub Actions workflow file
- Clear build step definitions
- Clear deployment step definitions
- Trigger rules matching the documentation project
- Necessary permission configurations
- Symlink, language suffix, and strict build checks
- GitHub Pages environment and deployment URL output
- Brief explanatory documentation or comments

## 12. Acceptance Criteria

Task is complete when all of the following conditions are met:

1. Repository contains an independent GitHub Actions workflow file.
2. Workflow triggers automatically on relevant main branch changes.
3. Workflow supports manual triggering.
4. Workflow completes checkout, environment setup, dependency installation, and build.
5. Workflow completes static artifact upload and deployment.
6. Deployment target natively supports GitHub Pages.
7. Workflow structure is clean and easy for maintainers to read and modify.
8. Workflow possesses value for reuse in other repositories.
9. Pull Requests run verification only without deploying; only `main` documentation changes trigger deployment.
10. Any broken, absolute, cyclic, or out-of-repo symlinks fail the build.
11. GitHub Pages displays top language switch, left navigation drawer, and stable URLs for both Chinese and English.
12. Workflow contains no emulator runs, external image downloads, or host network modifications.

## 13. Implementation Constraints

Implementers must observe the following constraints:

- Do not write documentation deployment scripts as generic placeholders detached from directory layout and security rules.
- Do not force unrelated CI/CD tasks into the same workflow.
- Do not trigger deployment on arbitrary code changes.
- Do not omit manual trigger capabilities.
- Do not omit basic build and deployment chains.
- Do not introduce overly complex designs exceeding requirements.
- Do not skip symlink checks, follow links outside repo, or copy authoritative Markdown into build directory to fake fixes.

## 14. Explicit Instructions for Implementers

Based on this PRD, design and implement a GitHub Actions build and deployment solution for the target repository:

- Focus on static documentation site build and deployment
- Support main branch automatic triggers
- Support manual triggers
- Prefer compatibility with GitHub Pages
- Use locked dependencies and strict build entry point under `docs-site/`
- Validate `.zh.md/.en.md` under `zh/`, `en/` and relative symlinks
- Use minimal Pages permissions; PRs do not deploy
- Use clean, stable, and maintainable workflow structure
- Minimize project-specific hacks
- Ensure output deliverables hold value for reuse

## 15. Expected Outcome

The final deliverable should be an automated solution serving the current emulator documentation site, strictly validating bilingual symlinks, deploying to GitHub Pages under least privilege, and suitable for long-term maintenance.

The solution satisfies the following purposes:

- Current repository can directly attach to auto-deployment
- Future repositories can reuse similar implementations
- Other AI agents can continue generating or modifying workflows based on the same standard
- Team members can quickly understand and maintain it
