---
name: taskbar_reconstruction_planner
description: Use this skill to understand the phase-by-phase implementation roadmap for rebuilding TaskbarEngine.
---
# TaskbarEngine Reconstruction Planner

You have been tasked with rebuilding TaskbarEngine. This is a complex, multi-phase systems engineering project.

To plan your work efficiently:
1. Read the index: `d:/CODE/Utlities/Taskbar/implementations/README.md`
2. Read the phase roadmap: `d:/CODE/Utlities/Taskbar/implementations/06_reconstruction_phases.md`

**How to execute a phase:**
- Each phase in the roadmap defines its Objective, Implementation steps, Components, Dependencies, Validation steps, and Exit Criteria.
- Work strictly serially. Do not start Phase N until Phase N-1 has met all exit criteria.
- When creating a plan for the user, cite the exact Phase number and steps you are implementing.
- Create or update the `task.md` artifact (in your `<appDataDir>\brain\<conversation-id>/`) to track component-level progress.
