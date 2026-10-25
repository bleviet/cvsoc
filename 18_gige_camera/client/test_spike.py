#!/usr/bin/env python3
"""Loopback self-test for the M0 spike tools (gvsp_blast / gvsp_sink).

Runs both tools on 127.0.0.1, so no board is required. Checks that the
packet accounting is exact and that pacing and argument validation work.
Run via `make test` (which builds the host binaries first).
"""

import json
import pathlib
import subprocess
import time
import unittest

BIN = pathlib.Path(__file__).resolve().parent.parent / "software" / "spike" / "build" / "host"
BLAST = BIN / "gvsp_blast"
SINK = BIN / "gvsp_sink"
PORT = "50111"


def start_sink(*extra):
    """Start gvsp_sink and wait until it is bound to the port."""
    proc = subprocess.Popen(
        [str(SINK), "-p", PORT, "-j", "-i", "1", "-w", "10", "-B", "4194304", *extra],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        line = proc.stderr.readline()
        if "listening" in line:
            return proc
    proc.kill()
    raise RuntimeError("gvsp_sink did not start")


def last_json(text):
    return json.loads(text.strip().splitlines()[-1])


class SpikeLoopbackTest(unittest.TestCase):

    def run_pair(self, *blast_args):
        sink = start_sink()
        try:
            blast = subprocess.run(
                [str(BLAST), "-d", "127.0.0.1", "-p", PORT, "-j", *blast_args],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(blast.returncode, 0, blast.stderr)
            out, _ = sink.communicate(timeout=15)
        finally:
            sink.kill()
        return last_json(blast.stdout), last_json(out)

    def test_paced_stream_is_lossless_and_accounted(self):
        tx, rx = self.run_pair("-t", "1.5", "-r", "100", "-b", "8")
        self.assertEqual(rx["packets"], tx["packets"])
        self.assertEqual(rx["bytes"], tx["bytes"])
        self.assertEqual(rx["lost"], 0)
        self.assertEqual(rx["malformed"], 0)
        self.assertAlmostEqual(tx["payload_mbps"], 100.0, delta=5.0)

    def test_frames_wrap_at_frame_boundary(self):
        # 10000-byte frames, 1000 data bytes/packet -> exactly 10 packets/frame.
        tx, rx = self.run_pair("-t", "0.5", "-r", "20", "-s", "1016", "-f", "10000")
        self.assertGreater(tx["frames"], 0)
        self.assertEqual(rx["packets"], tx["packets"])
        self.assertEqual(tx["bytes"], tx["packets"] * 1016)

    def test_json_summary_fields(self):
        tx, rx = self.run_pair("-t", "0.5", "-r", "50")
        for key in ("payload_mbps", "wire_mbps", "pps", "fps", "proc_cpu_pct",
                    "cpu_busy_pct", "cpu_softirq_pct", "per_core_busy_pct"):
            self.assertIn(key, tx)
        for key in ("loss_pct", "reordered", "payload_mbps", "pps"):
            self.assertIn(key, rx)
        self.assertGreater(tx["wire_mbps"], tx["payload_mbps"])

    def test_rejects_invalid_arguments(self):
        for args in (["-s", "16"], ["-s", "70000"], ["-b", "0"], ["-b", "999"], []):
            dest = ["-d", "127.0.0.1"] if args else []
            proc = subprocess.run([str(BLAST), *dest, *args],
                                  capture_output=True, text=True)
            self.assertEqual(proc.returncode, 2, args)


if __name__ == "__main__":
    unittest.main(verbosity=2)
