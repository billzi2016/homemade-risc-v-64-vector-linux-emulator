# Docs Site

This directory contains the MkDocs configuration and GitHub Pages workflow support for the project documentation site.

The authoritative Markdown files remain in the repository root, `docs/`, and `specs/`. The files under `docs-site/docs/` are relative symlinks only:

- `docs-site/docs/zh/**/*.zh.md` are Simplified Chinese entries.
- `docs-site/docs/en/**/*.md` are English entries without a language suffix.

Local validation:

```sh
python3 docs-site/scripts/check_docs.py
python3 -m pip install -r docs-site/requirements.lock
mkdocs build --strict --config-file docs-site/mkdocs.yml
```

The workflow does not run the emulator, download firmware/kernel/rootfs artifacts, or modify host networking.
