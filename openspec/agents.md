# Agent Guidelines

This file describes the expectations for automated agents, contributors, and reviewers working on the OpenSpec artifacts in this repository.

Purpose
- Provide a single-location reference for agent behaviour and contribution conventions for the `openspec/` tree.

Language
- All comments, specifications, proposals, designs, tasks and related documentation inside `openspec/` MUST be written in English. This ensures consistent review, searchability, and wider collaboration.

Agent behaviour
- Agents or automation that create, edit, or translate spec artifacts MUST:
  - Operate on files under `openspec/changes/` and `openspec/specs/` only when authorized by a human reviewer.
  - Preserve normative wording (MUST/SHALL) and scenario/test blocks verbatim when translating or converting formats.
  - Add `.en.md` translations alongside originals only when requested; do not delete original language files without explicit approval.
  - Ensure that every change includes unit tests: artifacts that modify or add behavior MUST include testable requirements and a corresponding test task in `tasks.md`.
  - When generating or updating a `tasks.md`, include a dedicated "Tests" phase (unit tests + integration tests) with explicit, runnable verification steps (e.g., `pio test`, `pio run -e native`).

Contribution notes
- Use the schema and templates provided by `openspec` when generating files.
- Keep normative requirements in English and use `SHALL/MUST` for normative language.
- When in doubt about phrasing or scope, open a short issue or PR describing the ambiguity.

Enforcement
- CI/job checks MUST verify that new or modified spec files under `openspec/` are written in English and include required headers (e.g., `## ADDED Requirements`).
- CI/job checks SHOULD also verify that each change directory (`openspec/changes/<name>/`) contains a `tasks.md` that includes at least one Tests phase with checkable `- [ ]` tasks. Failing checks should request human review.

Location
- This file is authoritative for agent behaviour related to the `openspec/` folder. For repository-wide contribution guidelines, see the top-level `README` or `CONTRIBUTING.md` if present.
