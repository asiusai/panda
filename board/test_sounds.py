#!/usr/bin/env python3
"""Play openpilot sounds on asius panda. All sounds by default, or one by name/number."""
import sys
import time

from panda import Panda

SOUNDS = {
    1: "engage",
    2: "disengage",
    3: "prompt",
    4: "refuse",
    5: "warning_soft",
    6: "warning_imm",
}

p = Panda()

if len(sys.argv) > 1:
    arg = sys.argv[1]
    if arg.isdigit():
        ids = [int(arg)]
    else:
        ids = [k for k, v in SOUNDS.items() if v == arg]
        if not ids:
            print(f"Unknown sound: {arg}")
            print(f"Available: {', '.join(SOUNDS.values())}")
            sys.exit(1)
else:
    ids = list(SOUNDS.keys())

for i in ids:
    print(f"Playing {SOUNDS[i]} ({i}/{len(SOUNDS)})")
    p.set_siren(True, i)
    time.sleep(3)

p.set_siren(False)
