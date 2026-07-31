"""Increment a small durable counter in the user's XDG data directory."""

from __future__ import annotations

import json

from kilix_state import Store


def main() -> None:
    with Store(
        "kilix-state-py-example", "counter.json", max_payload=4096
    ) as state:
        value = json.loads(state.load_or(b'{"launches": 0}'))
        value["launches"] += 1
        state.save(json.dumps(value, sort_keys=True).encode("utf-8"))
        print(f"launches: {value['launches']}")
        print(f"state: {state.path}")


if __name__ == "__main__":
    main()
