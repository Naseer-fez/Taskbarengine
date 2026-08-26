# AI Agent Master Execution Prompt

**Active Phase:** `docs/phases/phase_3_taskbar_resize.md` <!-- USER: Replace with current -->

## Objective
Execute the active phase strictly following project architecture and phase deliverables.

## Execution Rules
1. **Context**: Reference `docs/design_decisions.md` for global architecture rules. Global rules ALWAYS override phase-specific rules.
2. **Tools**: Prioritize read-only tools to verify assumptions before making changes.
3. **Validation**: Before finishing, verify:
   - Adherence to global and phase constraints.
   - All deliverables are met exactly as requested.
   - No memory leaks or unsafe Explorer operations.
4. **Output**: Format your final output according to the active phase's deliverables.
