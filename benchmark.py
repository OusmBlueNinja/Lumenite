#!/usr/bin/env python3
import time
import requests

def test_count(url):
    start = time.monotonic()
    resp = requests.get(url)
    elapsed = time.monotonic() - start

    # assume your Lua route returns JSON like: { "total": 1000000 }
    data = resp.json()
    total = data.get("total")

    print(f"Called {url}")
    print(f"→ Count returned: {total}")
    print(f"→ Elapsed time: {elapsed*1000:.2f} ms")

if __name__ == "__main__":
    # adjust port/path if needed
    url = "http://localhost:8080/count"
    test_count(url)
