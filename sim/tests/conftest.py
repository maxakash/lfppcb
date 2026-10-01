import json
import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.dirname(HERE)
ROOT = os.path.dirname(SIM)
sys.path.insert(0, SIM)

RESULTS = {}
OUT = os.path.join(SIM, "results", "summary.json")
IMG = os.path.join(ROOT, "docs", "images")


@pytest.fixture(scope="session")
def record():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    os.makedirs(IMG, exist_ok=True)

    def _rec(test, **kv):
        RESULTS.setdefault(test, {}).update({k: (float(v) if hasattr(v, "__float__") and not isinstance(v, (str, bool)) else v)
                                             for k, v in kv.items()})
    yield _rec
    old = {}
    if os.path.exists(OUT):
        try:
            old = json.load(open(OUT))
        except Exception:
            old = {}
    old.update(RESULTS)
    json.dump(old, open(OUT, "w"), indent=2, sort_keys=True)


@pytest.fixture(scope="session")
def img_dir():
    os.makedirs(IMG, exist_ok=True)
    return IMG
