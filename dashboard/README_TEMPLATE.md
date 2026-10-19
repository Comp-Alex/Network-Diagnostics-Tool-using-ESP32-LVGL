# Engineering Dashboard for Network-Diagnostics-Tool-using-ESP32-LVGL

This folder contains a small generator that produces an ESP32-focused engineering dashboard
(DASHBOARD.md) describing current functionality, architecture, roadmap, progress, and GitHub metrics.

Files:
- dashboard.py — the generator script (Python 3, stdlib only)
- config.json — generator configuration (output path and defaults)
- assets/ — place project images, logos, diagrams here (optional)

Quick start

1. Ensure Python 3 is installed and you're at the repository root.
2. (Optional) Set GITHUB_TOKEN environment variable if you want upstream GitHub metrics to be fetched:
   - Windows PowerShell: $env:GITHUB_TOKEN = '<token>'
   - Linux/macOS: export GITHUB_TOKEN='<token>'
3. Run:
   python dashboard/dashboard.py

Generated output:
- dashboard/output/DASHBOARD.md — generated Markdown dashboard

Suggested automation

A GitHub Action (existing workflow may already exist at .github/workflows/dashboard.yml) can call this script on push to update the dashboard automatically and commit the result.

Customization

Edit dashboard/config.json to tweak the roadmap defaults or output path.

Notes

- The generator is intentionally conservative: it uses simple heuristics to extract modules, functions, TODOs, and basic git metrics. It is meant as an engineering starting point — refine rules or add GitHub API token support for richer metrics.
