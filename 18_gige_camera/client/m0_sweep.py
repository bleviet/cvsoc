#!/usr/bin/env python3
"""M0 throughput sweep: DE10-Nano gvsp_blast -> PC gvsp_sink.

For every (payload size, sendmmsg batch) combination, starts gvsp_sink
locally, runs gvsp_blast on the board over ssh, and records both JSON
summaries as one line in results/m0-<timestamp>.jsonl. A table is printed
at the end for ADR-001.

  python3 client/m0_sweep.py --host 192.168.1.100 --sizes 1472,8972 --batches 1,32
  python3 client/m0_sweep.py --host 192.168.1.100 --probe-mtu

Payloads above 1472 bytes need jumbo frames on BOTH ends: raise the PC NIC
MTU yourself (sudo ip link set <iface> mtu 9000) and pass --board-mtu so the
board's eth0 is raised too.
"""

import argparse
import datetime
import json
import pathlib
import socket
import subprocess
import sys
import time

PROJECT = pathlib.Path(__file__).resolve().parent.parent
SINK = PROJECT / "software" / "spike" / "build" / "host" / "gvsp_sink"
RESULTS = PROJECT / "results"
REMOTE_BLAST = "/tmp/gvsp_blast"
MTU_CANDIDATES = (9000, 8000, 7000, 6000, 5000, 4000, 3500, 3000, 2500, 2000, 1500)


def ssh(args, command, timeout):
    target = f"{args.user}@{args.host}" if args.user else args.host
    return subprocess.run(
        ["ssh", "-o", "StrictHostKeyChecking=accept-new", target, command],
        capture_output=True, text=True, timeout=timeout)


def start_sink(args):
    """Start gvsp_sink (locally or on --sink-host) and wait until it is bound."""
    cmd = [args.sink_bin, "-p", str(args.port), "-j", "-i", "3",
           "-w", str(args.duration + 30)]
    if args.sink_host:
        cmd = ["ssh", args.sink_host, " ".join(cmd)]
    sink = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True)
    for line in sink.stderr:
        if "listening" in line:
            return sink
    sink.kill()
    sys.exit(f"gvsp_sink did not start: {' '.join(cmd)}")


def local_ip_towards(host):
    """Source address this PC uses to reach the board."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.connect((host, 9))
        return s.getsockname()[0]


def last_json(text):
    for line in reversed(text.strip().splitlines()):
        if line.startswith("{"):
            return json.loads(line)
    return None


def probe_mtu(args):
    """Largest MTU the board's interface accepts. Restores 1500 afterwards.

    Changing the MTU can reset the EMAC and drop this ssh session, so the
    probe runs detached on the board and the result is read back separately.
    """
    loop = " ".join(str(m) for m in MTU_CANDIDATES)
    probe = (f"for m in {loop}; do "
             f"ip link set {args.iface} mtu $m 2>/dev/null && {{ echo $m; break; }}; "
             f"done; ip link set {args.iface} mtu 1500; echo done")
    ssh(args, f"rm -f /tmp/m0_mtu.txt; nohup sh -c '{probe}' >/tmp/m0_mtu.txt 2>&1 &",
        timeout=30)
    for _ in range(10):
        time.sleep(2)
        try:
            res = ssh(args, "cat /tmp/m0_mtu.txt", timeout=15)
        except subprocess.TimeoutExpired:
            continue
        lines = res.stdout.split()
        if "done" in lines:
            break
    else:
        sys.exit("MTU probe did not finish; check the board's eth0 MTU by hand")
    if not lines[0].isdigit():
        sys.exit(f"MTU probe failed: {res.stdout.strip()}")
    mtu = int(lines[0])
    print(f"Largest MTU accepted by {args.iface} on {args.host}: {mtu} "
          f"(max UDP payload {mtu - 28} bytes). {args.iface} restored to 1500.")
    return mtu


def run_one(args, dest_ip, size, batch):
    sink = start_sink(args)
    try:
        blast = ssh(args,
                    f"{REMOTE_BLAST} -d {dest_ip} -p {args.port} -s {size} "
                    f"-b {batch} -t {args.duration} -j",
                    timeout=args.duration + 60)
        sink_out, _ = sink.communicate(timeout=60)
    finally:
        sink.kill()

    record = {"payload": size, "batch": batch,
              "blast_rc": blast.returncode, "tx": last_json(blast.stdout),
              "rx": last_json(sink_out)}
    if blast.returncode == 3:
        record["error"] = "mtu_too_small"
    elif blast.returncode != 0:
        record["error"] = blast.stderr.strip()[-300:]
    return record


def print_table(records):
    head = (f"{'payload':>7} {'batch':>5} {'tx Mbit/s':>9} {'rx Mbit/s':>9} "
            f"{'kpkt/s':>7} {'fps':>6} {'loss %':>7} {'CPU %':>6} {'softirq %':>9}")
    print("\n" + head + "\n" + "-" * len(head))
    for r in records:
        tx, rx = r.get("tx"), r.get("rx")
        if r.get("error") or not tx or not rx:
            print(f"{r['payload']:>7} {r['batch']:>5}  {r.get('error', 'no data')}")
            continue
        print(f"{r['payload']:>7} {r['batch']:>5} {tx['payload_mbps']:>9.1f} "
              f"{rx['payload_mbps']:>9.1f} {tx['pps'] / 1000:>7.1f} {tx['fps']:>6.2f} "
              f"{rx['loss_pct']:>7.3f} {tx['cpu_busy_pct']:>6.1f} "
              f"{tx['cpu_softirq_pct']:>9.1f}")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", required=True, help="board IP or ssh alias")
    p.add_argument("--user", help="board ssh user (default: from ssh config)")
    p.add_argument("--iface", default="eth0", help="board network interface")
    p.add_argument("--port", type=int, default=50010)
    p.add_argument("--dest-ip", help="sink address the board sends to (default: auto)")
    p.add_argument("--sink-host", help="ssh host that runs gvsp_sink (default: this PC)")
    p.add_argument("--sink-bin", default=str(SINK),
                   help="gvsp_sink path (on --sink-host if given)")
    p.add_argument("--sizes", default="1472", help="comma-separated UDP payload sizes")
    p.add_argument("--batches", default="1,8,32,64", help="comma-separated sendmmsg batches")
    p.add_argument("--duration", type=int, default=10, help="seconds per run")
    p.add_argument("--board-mtu", type=int, help="set board MTU before the sweep")
    p.add_argument("--probe-mtu", action="store_true", help="only probe the max MTU")
    args = p.parse_args()

    if args.probe_mtu:
        probe_mtu(args)
        return
    if args.sink_host and not args.dest_ip:
        sys.exit("--sink-host needs --dest-ip (the sink host's address on the board link)")
    if not args.sink_host and not pathlib.Path(args.sink_bin).exists():
        sys.exit(f"{args.sink_bin} missing: run 'make spike-host' first")

    if args.board_mtu:
        res = ssh(args, f"ip link set {args.iface} mtu {args.board_mtu}", timeout=30)
        if res.returncode != 0:
            sys.exit(f"Board rejected MTU {args.board_mtu}: {res.stderr.strip()}")

    dest_ip = args.dest_ip or local_ip_towards(args.host)
    RESULTS.mkdir(exist_ok=True)
    out = RESULTS / f"m0-{datetime.datetime.now():%Y%m%d-%H%M%S}.jsonl"
    sizes = [int(s) for s in args.sizes.split(",")]
    batches = [int(b) for b in args.batches.split(",")]

    print(f"Sweep: board {args.host} -> {dest_ip}, sizes {sizes}, batches {batches}, "
          f"{args.duration} s each. Results: {out.relative_to(PROJECT)}")
    records = []
    with out.open("w") as f:
        for size in sizes:
            for batch in batches:
                print(f"  payload {size:>5} batch {batch:>3} ...", flush=True)
                rec = run_one(args, dest_ip, size, batch)
                records.append(rec)
                f.write(json.dumps(rec) + "\n")
                f.flush()
    print_table(records)


if __name__ == "__main__":
    main()
