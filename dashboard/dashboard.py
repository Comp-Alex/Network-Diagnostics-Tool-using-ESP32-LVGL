#!/usr/bin/env python3
"""
ESP32 Engineering Dashboard generator for Network-Diagnostics-Tool-using-ESP32-LVGL

Usage:
  python dashboard.py [--output dashboard/output/DASHBOARD.md]

What it does:
- Scans repository files (README.md, platformio.ini, src/) to produce a Markdown dashboard
- Gathers local git metrics and, if GITHUB_TOKEN present and remote origin is GitHub, attempts GitHub API metrics
- Produces sections: Current functionality, Project architecture, Development roadmap, Feature checklist, Progress tracker, GitHub metrics

No external Python packages required.
"""

from __future__ import annotations
import argparse
import json
import os
import re
import subprocess
import sys
import datetime
from pathlib import Path
from typing import List, Dict, Optional

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_OUTPUT = SCRIPT_DIR / "output" / "DASHBOARD.md"
CONFIG_PATH = SCRIPT_DIR / "config.json"

# heuristics
SRC_DIR = REPO_ROOT / "src"
README = REPO_ROOT / "README.md"
PLATFORMIO = REPO_ROOT / "platformio.ini"

# regex patterns for simple extraction
NAMESPACE_RE = re.compile(r"namespace\s+([A-Za-z0-9_]+)")
FUNCTION_RE = re.compile(r"(?:\b(?:void|bool|int|String|uint8_t|uint32_t)\b)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")
TODO_RE = re.compile(r"//\s*TODO|/\*\s*TODO|FIXME", re.IGNORECASE)
PIN_DEFINE_RE = re.compile(r"(TftBacklightPin|TouchSckPin|TouchMisoPin|TouchMosiPin|TouchCsPin|TouchIrqPin|SdCsPin|AlertSpeakerPin)\s*=|#define\s+([A-Za-z0-9_]+)\s+(\d+)")


def read_file(path: Path) -> str:
    try:
        return path.read_text(encoding='utf-8', errors='replace')
    except Exception:
        return ""


def list_source_files() -> List[Path]:
    files = []
    if SRC_DIR.exists():
        for ext in ("*.cpp", "*.c", "*.h", "*.hpp", "*.ino"):
            files.extend(sorted(SRC_DIR.rglob(ext)))
    return files


def extract_modules(files: List[Path]) -> Dict[str, Dict]:
    modules = {}
    for f in files:
        text = read_file(f)
        # name by file
        name = f.stem
        modules[name] = modules.get(name, {'files': [], 'namespaces': [], 'functions': [], 'todos': 0})
        modules[name]['files'].append(str(f.relative_to(REPO_ROOT)))
        # namespaces
        for m in NAMESPACE_RE.findall(text):
            if m not in modules[name]['namespaces']:
                modules[name]['namespaces'].append(m)
        # functions
        for fn in re.findall(r"\b(?:void|bool|int|String|uint8_t|uint32_t|uint16_t|uint64_t)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", text):
            if fn not in modules[name]['functions']:
                modules[name]['functions'].append(fn)
        # todos
        modules[name]['todos'] += len(TODO_RE.findall(text))
    return modules


def find_todos(files: List[Path]) -> List[str]:
    out = []
    for f in files:
        text = read_file(f)
        for i, line in enumerate(text.splitlines(), start=1):
            if TODO_RE.search(line):
                out.append(f"{f.relative_to(REPO_ROOT)}:{i}: {line.strip()}")
    return out


def git_local_metrics() -> Dict:
    def run(cmd):
        try:
            out = subprocess.check_output(cmd, cwd=REPO_ROOT, shell=True, stderr=subprocess.DEVNULL)
            return out.decode('utf-8', errors='replace').strip()
        except subprocess.CalledProcessError:
            return ""

    metrics = {}
    metrics['current_branch'] = run('git rev-parse --abbrev-ref HEAD')
    metrics['last_commit'] = run('git log -1 --pretty=format:"%h %ci %s"')
    metrics['commit_count'] = run('git rev-list --count HEAD')
    authors = run('git shortlog -sne --all')
    metrics['contributors'] = [line.strip() for line in authors.splitlines() if line.strip()]
    # recent activity
    recent = run("git log --since='30 days ago' --pretty=format:'%h|%ci|%s' -n 100")
    metrics['recent_commits_30d'] = [l for l in recent.splitlines() if l]
    return metrics


def github_remote() -> Optional[str]:
    try:
        out = subprocess.check_output('git remote get-url origin', cwd=REPO_ROOT, shell=True).decode().strip()
        return out
    except Exception:
        return None


def github_api_metrics(remote_url: str, repo_override: Optional[str] = None) -> Dict:
    """Attempt to query GitHub API if GITHUB_TOKEN in env. Expects remote_url like https://github.com/owner/repo.git or git@github.com:owner/repo.git"""
    token = os.environ.get('GITHUB_TOKEN') or os.environ.get('GH_TOKEN')
    if not token:
        return {}
    # parse owner/repo
    m = re.search(r"github.com[:/](?P<owner>[^/]+)/(?P<repo>[^/.]+)", remote_url)
    if not m:
        return {}
    owner = m.group('owner')
    repo = m.group('repo')
    repo_id = f"{owner}/{repo}"
    api = f"https://api.github.com/repos/{owner}/{repo}"
    import urllib.request
    headers = {'Authorization': f'token {token}', 'User-Agent': 'ndt-dashboard'}
    def get_json(url):
        try:
            req = urllib.request.Request(url, headers=headers)
            with urllib.request.urlopen(req, timeout=10) as r:
                import json
                return json.load(r)
        except Exception:
            return None
    repo_data = get_json(api)
    issues = get_json(api + '/issues?state=open&per_page=1')
    pulls = get_json(api + '/pulls?state=open&per_page=1')
    metrics = {}
    if repo_data:
        metrics['stars'] = repo_data.get('stargazers_count')
        metrics['forks'] = repo_data.get('forks_count')
        metrics['open_issues'] = repo_data.get('open_issues_count')
        metrics['description'] = repo_data.get('description')
        metrics['html_url'] = repo_data.get('html_url')
    # try to get counts from headers if paginated
    return metrics


def build_dashboard(output_path: Path):
    files = list_source_files()
    modules = extract_modules(files)
    todos = find_todos(files)
    readme_text = read_file(README)
    platformio_text = read_file(PLATFORMIO) if PLATFORMIO.exists() else ''

    # sections
    now = datetime.datetime.utcnow().isoformat() + 'Z'
    lines: List[str] = []
    lines.append(f"# ESP32 Engineering Dashboard\n\n_Generated: {now}_\n")

    # Current functionality
    lines.append("## Current functionality\n")
    if readme_text:
        # include first paragraph of README
        para = '\n'.join([l for l in readme_text.splitlines() if l.strip()][:12])
        lines.append(para + '\n')
    else:
        lines.append("Repository README not found.\n")

    lines.append("### Detected modules and responsibilities\n")
    for name, meta in sorted(modules.items()):
        funcs = ', '.join(meta['functions'][:6]) or '—'
        ns = ', '.join(meta['namespaces']) or '—'
        lines.append(f"- **{name}** — files: {len(meta['files'])}; namespaces: {ns}; example functions: {funcs}")
    lines.append('')

    # Project architecture
    lines.append('## Project architecture\n')
    lines.append('Architecture summary (auto-extracted):\n')
    lines.append('- MCU: ESP32 (Arduino framework)\n- UI: LVGL + TFT_eSPI + XPT2046 touch\n- Key services: Diagnostics (ICMP), SubnetScanner (TCP checks), TrafficMonitor (promiscuous frames), WIDS guard (alerts), Storage (SD/NVS), Time (NTP / optional RTC)\n')

    # show pin mapping if Config.h present
    config_h = SRC_DIR / 'Config.h'
    if config_h.exists():
        cfg = read_file(config_h)
        pins = []
        for line in cfg.splitlines():
            if 'Pin' in line and '=' in line:
                pins.append(line.strip())
        if pins:
            lines.append('### Pin configuration (from src/Config.h)')
            lines.extend([f'- {p}' for p in pins])
            lines.append('')

    # Development roadmap
    lines.append('## Development roadmap\n')
    # heuristics: TODO items and open suggestions
    lines.append('Proposed roadmap (auto-generated suggestions):\n')
    roadmap = [
        'Polish Wi‑Fi connection persistence and credential storage (NVS)',
        'Add more precise LAN device discovery and optional mDNS support',
        'Improve false-positive handling for WIDS heuristics and add configurable sensitivity',
        'Add optional packet capture/logging mode (user opt-in) and secure export',
        'Add OTA update support and CI build artifacts',
        'Implement automated tests or hardware-in-loop validation flows'
    ]
    for i, r in enumerate(roadmap, start=1):
        lines.append(f'- [ ] {r}')
    lines.append('')

    # Feature checklist
    lines.append('## Feature checklist\n')
    features = [
        ('Diagnostics (ICMP gateway/DNS probes)', 'DiagnosticsManager' in modules),
        ('Local subnet TCP discovery', 'SubnetScanner' in modules),
        ('Traffic Monitor (passive 2.4 GHz)', 'TrafficMonitor' in modules),
        ('WIDS Guard (management-frame watch)', 'WidsGuard' in modules or 'WIDS' in modules),
        ('Touchscreen LVGL UI', any('main' in m for m in modules)),
        ('SD card logging', 'StorageManager' in modules),
        ('NTP / optional RTC', 'TimeManager' in modules),
    ]
    for feat, exists in features:
        mark = 'x' if exists else ' '
        lines.append(f"- [{mark}] {feat}")
    lines.append('')

    # Progress tracker
    lines.append('## Progress tracker\n')
    total_features = len(features)
    completed = sum(1 for _, e in features if e)
    todo_count = len(todos)
    try:
        percent = int((completed / total_features) * 100) if total_features else 0
    except Exception:
        percent = 0
    lines.append(f'- Implemented features: {completed}/{total_features} ({percent}%)')
    lines.append(f'- TODO items found in code: {todo_count}')
    lines.append('')
    lines.append('### Recent TODOs')
    for t in todos[:12]:
        lines.append(f'- {t}')
    if not todos:
        lines.append('- None found')
    lines.append('')

    # Git / GitHub metrics
    lines.append('## Git / GitHub metrics\n')
    git_metrics = git_local_metrics()
    lines.append(f"- Current branch: {git_metrics.get('current_branch')}")
    lines.append(f"- Last commit: {git_metrics.get('last_commit')}")
    lines.append(f"- Commit count: {git_metrics.get('commit_count')}")
    lines.append(f"- Contributors (top):")
    for c in git_metrics.get('contributors', [])[:8]:
        lines.append(f'  - {c}')

    remote = github_remote()
    if remote:
        lines.append(f"- Remote origin: {remote}")
        gh = github_api_metrics(remote)
        if gh:
            lines.append(f"- GitHub stars: {gh.get('stars', 'n/a')}")
            lines.append(f"- GitHub forks: {gh.get('forks', 'n/a')}")
            lines.append(f"- Open issues (repo): {gh.get('open_issues', 'n/a')}")
        else:
            lines.append('- GitHub API metrics not available (GITHUB_TOKEN not set or request failed)')
    else:
        lines.append('- No git origin discovered')

    # Write output
    outp = Path(config.get('output', str(DEFAULT_OUTPUT))) if 'config' in globals() else DEFAULT_OUTPUT
    try:
        outp = Path(outp)
    except Exception:
        outp = DEFAULT_OUTPUT
    outp.parent.mkdir(parents=True, exist_ok=True)
    outp.write_text('\n'.join(lines), encoding='utf-8')
    print(f'Wrote dashboard to {outp}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Generate ESP32 engineering dashboard')
    parser.add_argument('--output', '-o', help='Output markdown path', default=None)
    parser.add_argument('--config', '-c', help='Path to dashboard config.json', default=str(CONFIG_PATH))
    args = parser.parse_args()

    config = {}
    try:
        if args.config and Path(args.config).exists():
            config = json.loads(Path(args.config).read_text(encoding='utf-8'))
    except Exception:
        config = {}

    if args.output:
        outp = Path(args.output)
        config['output'] = str(outp)
    if 'output' not in config:
        config['output'] = str(DEFAULT_OUTPUT)

    build_dashboard(Path(config['output']))
