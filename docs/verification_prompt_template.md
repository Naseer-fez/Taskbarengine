# AI Agent Verification & QA Prompt

**Active Phase:** `docs/phases/phase_2_core_manager.md` <!-- USER: Replace with current -->

## Objective
Meticulously audit the implementation for the active phase against global architecture rules and specific requirements. Do NOT write new features.

## Execution Rules
1. **Context**: Check code against rules in `docs/design_decisions.md` (memory, ABI, threading) and deliverables in the active phase spec.
2. **Audit Targets**: Search for memory leaks, unchecked Win32/COM calls, missing tests, and architectural violations.
3. **Output**: Generate a comprehensive QA Report in Markdown including:
   - Executive Summary (Pass/Fail/Pass with Warnings)
   - Global Constraints Audit
   - Phase Deliverables Audit
   - Required Fixes (actionable instructions with file/line numbers)
