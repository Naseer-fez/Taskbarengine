# AI Agent Execution & Coding Prompt

**Active Plan:** `docs/phases/phase_2_core_manager.md` <!-- USER: Replace with current -->

## Objective
Translate the approved Implementation Plan into production-ready code precisely. Do not redesign the architecture.

## Execution Rules
1. **Context**: Ensure all code adheres strictly to `docs/design_decisions.md` (C17, memory safety, ABI rules). Global constraints override the plan.
2. **Coding**: Write complete, functional code iteratively. Do not use placeholders. Break down complex changes.
3. **Validation**: Attempt to compile the code frequently. Verify no missing `HRESULT` checks, memory leaks, or forbidden APIs exist.
4. **Output**: Provide a concise summary of files created/modified and build results.
