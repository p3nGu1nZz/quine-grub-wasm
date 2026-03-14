# Agent Workflows

This document summarises the higher-level Copilot agent workflows that are
available in the repository. They are provided as prompt templates under
`.github/prompts/` and orchestrate multiple skills in a fixed sequence.

## master_workflow.prompt.md

Used for normal development and release work.  When the agent is given the
contents of this prompt, it will:

1. Inspect the GitHub issue backlog and pick or propose tasks.
2. Run `code-review` to generate candidate issues if none exist.
3. Create branches and implement features or fixes.
4. Build, test, commit, and open pull requests.
5. Close issues and propose version tags once the backlog is cleared.

This is the workflow that drives work towards the `v0.0.1` release.

## dev_workflow.prompt.md

A maintenance routine designed for repository housekeeping.  It invokes
these skills in order:

- `search-memory`
- `improve-skills`
- `improve-src`
- `update-skills`
- `commit-push`
- `update-docs`
- `update-specs`
- `update-memory`

As a final optional step it will also perform simple issue triage: if any
GitHub issues are open the agent can list them and ask which one to
address next.  When the backlog is empty the workflow will invoke
`code-review` to produce candidate tasks and present them for user
selection.

Run this workflow whenever you need a periodic clean‑up pass on the code,
docs or agent metadata; feature development itself is typically driven by
`master_workflow` but a quiet maintenance run may still generate issues.

---

Both prompts can be executed by pasting their contents into a Copilot agent
query; the agent will then follow the instructions contained within to
complete the sequence of tasks.
