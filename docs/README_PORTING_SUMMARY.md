Porting Report Summary

Overview:
- The C++ port reproduces core functionality of the web app (wasm3 integration, parser, evolution engine, GUI using ImGui).

Critical issues to address before merge:
1. `src/main.cpp`: wrong SDL_Init check (treats success as failure).
2. `src/logger.cpp`: unsafe signal handler invoking IO; use atomic flag instead.
3. Missing headless/terminal mode and CLI parsing as documented.

Suggested next steps:
- Open an issue describing these with acceptance criteria.
- Create a PR that adds an issue template and references this issue.
