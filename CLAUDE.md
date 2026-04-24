# Project Instructions for AI Agents

This file provides instructions and context for AI coding agents working on this project.

## 🚨 HARD LIMIT: MAX 6 THREADS FOR ALL BUILD/TEST COMMANDS 🚨

**EVERY invocation of `cmake`, `cmake --build`, `ctest`, `make`, or `ninja` MUST be capped at 6 parallel threads/processes. NO EXCEPTIONS.**

- `cmake --build <dir> --parallel 6` (NEVER `--parallel` alone, NEVER higher)
- `ctest --parallel 6` or `CTEST_PARALLEL_LEVEL=6 ctest`
- `make -j6` (NEVER bare `-j`, NEVER `-j$(nproc)`)
- `ninja -j6`

This applies to every command you run, every subagent you spawn, every script you author, every CI config you touch. When spawning subagents (bd-worker, bd-autopilot, etc.) restate this limit in their prompt. If you see a build command without `-j6`/`--parallel 6`, fix it before running.

## Required Reading

At the start of every session, read:
- `docs/01_principles.md` — core design principles
- `docs/TODO_reversibility_deferrals.md` — outstanding reversibility deferrals

**When archiving a document** (moving to `docs/archive/`), also remove its entry from this Required Reading list. A document in the archive must not be a session dependency.

<!-- BEGIN BEADS INTEGRATION v:1 profile:minimal hash:ca08a54f -->
## Beads Issue Tracker

This project uses **bd (beads)** for issue tracking. Run `bd prime` to see full workflow context and commands.

### Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --claim  # Claim work
bd close <id>         # Complete work
```

### Rules

- Use `bd` for ALL task tracking — do NOT use TodoWrite, TaskCreate, or markdown TODO lists
- Run `bd prime` for detailed command reference and session close protocol
- Use `bd remember` for persistent knowledge — do NOT use MEMORY.md files

## Session Completion

**When ending a work session**, you MUST complete ALL steps below. Work is NOT complete until `git push` succeeds.

**MANDATORY WORKFLOW:**

1. **File issues for remaining work** - Create issues for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **PUSH TO REMOTE** - This is MANDATORY:
   ```bash
   git pull --rebase
   bd dolt push
   git push
   git status  # MUST show "up to date with origin"
   ```
5. **Clean up** - Clear stashes, prune remote branches
6. **Verify** - All changes committed AND pushed
7. **Hand off** - Provide context for next session

**CRITICAL RULES:**
- Work is NOT complete until `git push` succeeds
- NEVER stop before pushing - that leaves work stranded locally
- NEVER say "ready to push when you are" - YOU must push
- If push fails, resolve and retry until it succeeds
<!-- END BEADS INTEGRATION -->


## Build & Test

_Add your build and test commands here_

```bash
# Example:
# npm install
# npm test
```

## Project Context

Track all work using **beads (`bd`)** — see the Beads Issue Tracker section above. Do not use ad-hoc TODO lists.

## Architecture Overview

_Add a brief overview of your project architecture_

## Conventions & Patterns

_Add your project-specific conventions here_
