# AI Agent QA Remediation Prompt

**Active QA Report:** [Current QA Report]

## Objective
Systematically fix all issues in the active QA report (Critical > High > Medium > Low) to achieve a PASS status without introducing new features.

## Execution Rules
1. **Context**: Reference `docs/design_decisions.md` and the original phase specification if you need clarification on intended behavior. 
2. **Process**: Fix issues atomically. Compile and test (e.g., `ctest`) after each fix to prevent regressions.
3. **Validation**: Ensure EVERY required fix in the QA report is addressed.
4. **Output**: Update the QA report document to log resolutions and change status to PASS. Provide a summary of fixes applied.
