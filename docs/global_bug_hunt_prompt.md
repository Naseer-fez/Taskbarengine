# Comprehensive Codebase Bug Hunt & Remediation Prompt

## Objective
Audit the codebase, identify issues (build errors, test failures, architecture violations), report them, and execute approved fixes.

## Execution Rules
1. **Analysis**: Run the local build system and test suite. Check codebase against `docs/design_decisions.md`. 
2. **Reporting**: Create `bug_report.md` detailing file/line, issue type, description, and proposed fix for all issues found.
3. **Approval**: STOP and request user approval on the bug report before making any code changes.
4. **Remediation**: Once approved, apply fixes atomically, compiling and testing after each. 
5. **Output**: Update `bug_report.md` to mark issues as RESOLVED and provide a final build/test status summary.
