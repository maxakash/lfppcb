#!/usr/bin/env python3
"""Static checks of the embedded web UI (lfp8/webui_html.h).

* the PROGMEM raw string is well formed,
* every element id referenced from JavaScript exists in the HTML,
* no external resources (CDN) are referenced - the page must work offline,
* the JavaScript parses (node --check), if node is installed.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, "..", "lfp8", "webui_html.h"), encoding="utf-8").read()
m = re.search(r'R"LFP8HTML\((.*)\)LFP8HTML"', src, re.S)
if not m:
    sys.exit("webui_html.h: raw string not found")
html = m.group(1)
fails = 0

js = "\n".join(re.findall(r"<script>(.*?)</script>", html, re.S))
ids = set(re.findall(r'id="([^"]+)"', html))
dynamic = re.compile(r"^(s_.*|cv\d|ci\d|rv\d|ri\d|vn\d|in\d)$")  # created by JS
for ref in sorted(set(re.findall(r"\$\('#([A-Za-z0-9_]+)'\)", js))):
    if ref not in ids and not dynamic.match(ref):
        print("missing element id:", ref)
        fails += 1

for url in re.findall(r'(?:src|href)\s*=\s*"(https?://[^"]+)"', html):
    print("external resource (page must work offline):", url)
    fails += 1

node = shutil.which("node")
if node:
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as f:
        f.write(js)
        path = f.name
    r = subprocess.run([node, "--check", path], capture_output=True, text=True)
    os.unlink(path)
    if r.returncode != 0:
        print(r.stderr)
        fails += 1
    else:
        print("node --check: JavaScript OK")
else:
    print("node not installed - JavaScript syntax check skipped")

print("web UI: %d bytes HTML (%d bytes JS), %d problems" % (len(html.encode()), len(js.encode()), fails))
sys.exit(1 if fails else 0)
