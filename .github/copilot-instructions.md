
## VS Build Tools ENV in PowerShell

There's a ready script to activate dev variables for currently installed VS Build Tools: `${workspace}\Enter-VSDev.ps1`. Just run it once in the new PowerShell session, if needed.

## Git Commands

Use git, but never **push** to remote server without User explicit consent.

## Project Documentation

Document all important architectural decisions, design artifacts, and tasks in the `specs/architecture.md` and `specs/continuous_architecture_findings.md`.
This file should be updated as the project evolves.

Document there all important findings and changes to the architecture, tech stack, libraries, and project structure.


## Browsing for functionality

When you need to find where a certain functionality is implemented, first consult `specs/architecture.md` to see if it's documented there. Then proceed to search the source code.


## Current Active Task important details

Keep all important information about current effort, assumptions, decisions and objectives in `/spec/plan.md`. Consult it often to stay on track. Mark clearly what's done and what remains. If something is not clear, update the plan.

Plan HAS to work in coordination with `/spec/architecture.md` and `/spec/continuous_architecture_findings.md`.

## Python tools

If you need to use Python, make sure you are using local environment with pinned dependencies: `.venv`. Use available `uv` to manage and create it, then `.\.venv\Scripts\Activate.ps1` to activate it in PowerShell.

### Setting up Python environment

```powershell
# Create virtual environment
uv venv .venv

# Activate it
.\.venv\Scripts\Activate.ps1

# Install dependencies
uv pip install <package-name>

# Run Python scripts
.\.venv\Scripts\python.exe <script.py>
```

**Important**: Always use `.\.venv\Scripts\python.exe` or `.\.venv\Scripts\Activate.ps1` to ensure you're using the local environment, not the system Python.
