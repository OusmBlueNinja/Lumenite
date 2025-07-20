import time
import requests
import signal
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

stop_requested = False


def send_request(url):
    if stop_requested:
        return None
    with requests.Session() as session:
        start = time.monotonic()
        try:
            session.get(url)
            return time.monotonic() - start
        except Exception:
            return None


def signal_handler(sig, frame):
    global stop_requested
    print("\n[!] Ctrl+C detected. Cancelling all tasks...")
    stop_requested = True
    sys.exit(0)


signal.signal(signal.SIGINT, signal_handler)


def test_concurrent(url, total_requests=1000, concurrent_workers=100):
    timings = []
    print(f"Sending {total_requests} requests with {concurrent_workers} threads...")

    start_all = time.monotonic()
    try:
        with ThreadPoolExecutor(max_workers=concurrent_workers) as executor:
            futures = [executor.submit(send_request, url) for _ in range(total_requests)]
            for future in as_completed(futures):
                if stop_requested:
                    break
                result = future.result()
                if result is not None:
                    timings.append(result)
    except KeyboardInterrupt:
        print("\n[!] Benchmark interrupted.")

    total_time = time.monotonic() - start_all
    average = sum(timings) / len(timings) if timings else float("inf")
    rps = len(timings) / total_time if total_time > 0 else 0

    print(f"Completed: {len(timings)}")
    print(f"Avg time/req: {average:.6f} sec")
    print(f"Total test time: {total_time:.3f} sec")
    print(f"Throughput: {rps:.2f} req/s")


if __name__ == "__main__":
    test_concurrent("http://127.0.0.1:8080", total_requests=1000, concurrent_workers=6)
