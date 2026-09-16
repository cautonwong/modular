# Security Policy

## Supported versions

Security fixes are applied to the latest release on `main`.

| Version | Supported |
| ------- | --------- |
| 0.5.x   | Yes       |
| < 0.5   | No        |

## Reporting a vulnerability

Please report suspected vulnerabilities privately by opening a
[security advisory](https://github.com/cautonwong/modular/security/advisories/new)
rather than a public issue. Include:

- affected layer(s) and version/commit,
- a minimal reproduction or proof of concept,
- impact assessment (memory corruption, fault injection, event spoofing, ...).

We aim to acknowledge within 5 business days and to provide a remediation plan
within 15 business days.

## Scope notes

The architecture separates a certifiable core (`edge_module`, `sys`, `board`,
`infra`) from application modules (`app`). Framework code must remain free of
architecture-specific interrupt/register access and must not allocate at
runtime. Reports that demonstrate a violation of these invariants are in scope.
