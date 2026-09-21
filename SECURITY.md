# Security Policy

## Supported versions

| Version | Supported |
|---------|-----------|
| 0.1.x   | ✅ current |

## Reporting a vulnerability

Please **do not** open public issues for security problems.
Report them through [GitHub Security Advisories](https://github.com/edmundo738/renderlift/security/advisories/new) for this repository ("Privately report a vulnerability").

Include: affected version/commit, reproduction steps, impact (crash, code execution inside the game process, privilege issues), and any suggested fix. You can expect an acknowledgement within ~7 days.

## Scope notes for this project

- RenderLift uses API interception/injection techniques (graphics API hooks, proxy DLLs) that are also used by cheats. This project **does not** target anti-cheat bypass, and issues requesting help with online/anti-cheat evasion will be closed.
- Because injected code runs inside another process, we treat memory-safety bugs in hook paths as high priority.
- Never submit or request instructions for bypassing DRM, license checks, or anti-cheat systems.
