---
name: taskbar_reconstruction_planner
description: Use this skill to understand the phase-by-phase implementation roadmap for rebuilding TaskbarEngine.
---
# TaskbarEngine Reconstruction Planner

You have been tasked with rebuilding TaskbarEngine. This is a complex, multi-phase systems engineering project.

**CRITICAL TOKEN-SAVING RULE:** 
DO NOT read entire documentation files (like `06_reconstruction_phases.md`) into context. They are massive and waste tokens. 
Instead, when you need to know about a specific Phase (e.g., Phase 2), you MUST:
1. Run `grep_search` to find the exact line numbers for that phase (e.g., `grep_search` for "Phase 2" in `06_reconstruction_phases.md`).
2. Use `view_file` with `StartLine` and `EndLine` to read ONLY the section of the document that pertains to your current phase.

**Roadmap Information:**
- The phase roadmap is located at: `d:/CODE/Utlities/Taskbar/implementations/06_reconstruction_phases.md`

**How to execute a phase:**
- Each phase in the roadmap defines its Objective, Implementation steps, Components, Dependencies, Validation steps, and Exit Criteria.
- Work strictly serially. Do not start Phase N until Phase N-1 has met all exit criteria.
- When creating a plan for the user, cite the exact Phase number and steps you are implementing.
- Create or update the `task.md` artifact (in your `<appDataDir>\brain\<conversation-id>/`) to track component-level progress.
