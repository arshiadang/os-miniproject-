#!/usr/bin/env python3
"""
web/server.py - SomaiyaSat Mission Simulator (website backend).

Uses only Python's built-in libraries (nothing to install).
Every button on the page calls one route below. The route runs the REAL
compiled experiment program on Linux, then turns its output into simple
step-by-step events that the page shows one at a time.

    POST /api/pass        Step 1  ./exp4/mission --battery N [--fail]   (Exp 4 + AI)
    GET  /api/ai_jobs     Step 2  messages the AI chose in step 1
    POST /api/radio       Step 2  ./exp6/scheduler <jobs> <time slice>  (Exp 6)
    POST /api/sensors     Step 3  ./exp7/buffer [unsafe]                (Exp 7)
    POST /api/power       Step 4  ./exp9/bankers <task> <4 numbers>     (Exp 9)

Run from the project folder:   python3 web/server.py
Then open in a browser:        http://localhost:8000
If localhost does not open from Windows (WSL):  python3 web/server.py --any
"""
import csv
import json
import os
import re
import subprocess
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PORT = 8000

# simple names used on the website
ACTION_WORDS = {
    "TX_TTC": "Send health data",
    "TX_SSTV": "Send photo",
    "TX_VOICE": "Send voice",
    "SAFE_BEACON": "Battery saver beacon",
    "IDLE": "Wait (signal too weak)",
}
# message type -> (name on the website, seconds the radio needs to send it)
MESSAGE = {
    "TX_TTC": ("Health_data", 2),
    "TX_SSTV": ("Photo", 36),
    "TX_VOICE": ("Voice", 12),
    "SAFE_BEACON": ("Beacon", 2),
}
TASKS = ["Health-data task", "Photo task", "Voice task", "AI-brain task"]
RESOURCES = ["radio", "camera", "power", "memory"]


def run(cmd):
    """Run a program from the project folder; return (exit code, output)."""
    try:
        # stderr goes into the same stream so messages stay in the order they happened
        p = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, timeout=120)
        return p.returncode, p.stdout
    except FileNotFoundError:
        return 127, f"Program not found: {cmd[0]} - run 'make' first."
    except subprocess.TimeoutExpired:
        return 124, "Timed out"


def read_csv(rel):
    path = os.path.join(ROOT, rel)
    if not os.path.exists(path):
        return []
    with open(path) as f:
        return list(csv.DictReader(f))


def ev(text, kind="info", tag=""):
    """One event shown on the page. kind: info / ok / bad / warn."""
    return {"text": text, "kind": kind, "tag": tag}


# ---------------- Step 1: the pass (Exp 4 + AI) ----------------

def api_pass(body):
    if not os.path.exists(os.path.join(ROOT, "data/model.pkl")):
        run(["python3", "ai/train_model.py"])          # train the AI the first time
    try:
        battery = int(body.get("battery", 85))
    except (TypeError, ValueError):
        battery = -1
    cmd = ["./exp4/mission", "--battery", str(battery)] + (["--fail"] if body.get("broken") else [])
    code, out = run(cmd)

    events, child_job = [], {}
    for line in out.splitlines():
        m = re.search(r"created child (\d+) for (.+)$", line)
        if "ground pass started" in line:
            pid = re.search(r"pid=(\d+)", line)
            events.append(ev(f"Main program started (parent process, PID {pid.group(1) if pid else '?'})",
                             "info", "process"))
        elif m:
            pid, job = m.group(1), m.group(2)
            child_job[pid] = job
            what = {"telemetry acquisition": "read the satellite's sensors",
                    "AI router": "run the AI and decide what to send",
                    "report": "write the mission report"}.get(job, job)
            tag = "fork() + exec()" if job != "report" else "fork()"
            events.append(ev(f"Parent created child process {pid} to {what}", "info", tag))
        elif "[generate_pass] wrote" in line:
            n = re.search(r"wrote (\d+) rows", line)
            events.append(ev(f"Sensors read OK: {n.group(1) if n else '?'} readings, one every 10 seconds "
                             f"(battery started at {battery}%)", "ok", "write()"))
        elif "SENSOR FAILURE" in line:
            events.append(ev("Sensor is broken - no readings were collected", "bad"))
        elif "INVALID SENSOR READING" in line:
            events.append(ev(f"Impossible reading: battery {battery}% cannot exist, so the reading was rejected", "bad"))
        elif "[router] ERROR: data/telemetry.csv not found" in line:
            events.append(ev("AI looked for sensor readings but found nothing", "bad"))
        elif "[router]" in line and "decisions ->" in line:
            n = re.search(r"\] (\d+) decisions", line)
            events.append(ev(f"AI made {n.group(1) if n else '?'} decisions - one for every 10 seconds of the pass", "ok"))
        elif "safety overrides" in line:
            k = int(re.search(r"safety overrides: (\d+)", line).group(1))
            if k:
                events.append(ev(f"Safety rule stepped in {k} time(s): battery below 20%, so the AI's choice "
                                 "was changed to a battery saver beacon", "warn"))
        elif "finished, exit status" in line:
            mm = re.search(r"child (\d+) \((.+)\) finished, exit status = (-?\d+)", line)
            if mm:
                pid, job, st = mm.groups()
                if st == "0":
                    events.append(ev(f"Child {pid} finished OK (exit code 0). Parent was waiting for it", "ok", "waitpid()"))
                else:
                    events.append(ev(f"Child {pid} failed (exit code {st}). Parent noticed this", "bad", "waitpid()"))
        elif "entering SAFE MODE" in line:
            events.append(ev("Parent switches to SAFE MODE: only send short 'I am alive' beacons", "warn"))
        elif "ground pass finished" in line:
            events.append(ev("Report saved in logs/mission_report.txt (written with open/read/write)", "ok", "read() + write()"))

    decisions = read_csv("data/decisions.csv")
    counts = {}
    for d in decisions:
        counts[d["final_action"]] = counts.get(d["final_action"], 0) + 1
    safe_mode = "SAFE MODE" in out
    return {
        "ok": code == 0,
        "command": " ".join(cmd),
        "output": out,
        "events": events,
        "safe_mode": safe_mode,
        "decisions": [{"t": d["t_s"], "action": d["final_action"], "words": ACTION_WORDS.get(d["final_action"], ""),
                       "snr": d["snr_db"], "battery": d["battery_pct"], "changed": d["overridden"] == "yes"}
                      for d in decisions],
        "counts": {ACTION_WORDS[k]: counts.get(k, 0) for k in ACTION_WORDS},
    }


# ---------------- Step 2: one radio (Exp 6) ----------------

def api_ai_jobs(_):
    picks = [d for d in read_csv("data/decisions.csv") if d["final_action"] in MESSAGE]
    if not picks:
        return {"ok": False, "error": "No messages yet - run Step 1 first."}
    jobs = []
    for i, d in enumerate(picks[:6]):
        name, secs = MESSAGE[d["final_action"]]
        jobs.append({"name": name, "arrival": i, "burst": secs})
    return {"ok": True, "jobs": jobs}


def parse_schedule(out):
    results = []
    for block in re.split(r"(?m)^===== ", out)[1:]:
        title = block.split(" =====")[0]
        lines = block.splitlines()
        ids = next((ln for ln in lines if ln.startswith("|")), "")
        times = next((ln for ln in lines if re.match(r"^0\s", ln)), "")
        jobs = [j.strip() for j in ids.strip("|").split("|") if j.strip()]
        ends = [int(x) for x in times.split()[1:]]
        gantt, start = [], 0
        for j, e in zip(jobs, ends):
            gantt.append({"job": j, "start": start, "end": e})
            start = e
        rows = re.findall(r"(?m)^(\w+)\s+(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)$", block)
        wt = re.search(r"Average waiting time\s*=\s*([\d.]+)", block)
        tat = re.search(r"Average turnaround time\s*=\s*([\d.]+)", block)
        results.append({
            "title": title, "gantt": gantt,
            "avg_wait": float(wt.group(1)) if wt else None,
            "avg_turnaround": float(tat.group(1)) if tat else None,
            "rows": [{"id": r[0], "name": r[1], "arrival": int(r[2]), "burst": int(r[3]),
                      "finish": int(r[4]), "wait": int(r[6])} for r in rows],
        })
    return results


def api_radio(body):
    jobs = body.get("jobs") or []
    lines = ["# messages waiting for the radio (written by the website)", "# id name arrival burst"]
    for i, j in enumerate(jobs[:12]):
        try:
            name = re.sub(r"\W", "_", str(j.get("name", "Msg"))) or "Msg"
            lines.append(f"M{i + 1} {name} {int(j['arrival'])} {int(j['burst'])}")
        except (KeyError, ValueError, TypeError):
            return {"ok": False, "error": f"Message {i + 1} has a missing or non-number value."}
    if len(lines) == 2:
        return {"ok": False, "error": "Add at least one message."}
    with open(os.path.join(ROOT, "data/web_tasks.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    slice_s = str(body.get("slice", 4))
    cmd = ["./exp6/scheduler", "data/web_tasks.txt", slice_s]
    code, out = run(cmd)
    if code != 0:
        msg = out.strip().splitlines()[-1] if out.strip() else "error"
        friendly = msg
        if "time quantum" in msg:
            friendly = f"Time slice {slice_s} s is not allowed: a turn must be at least 1 second, otherwise the radio never sends anything."
        elif "invalid arrival/burst" in msg:
            friendly = "A message needs 0 seconds (or arrives before time 0). That is impossible, so the scheduler refused the whole list."
        return {"ok": False, "command": " ".join(cmd), "output": out, "error": friendly}
    return {"ok": True, "command": " ".join(cmd), "output": out, "results": parse_schedule(out)}


# ---------------- Step 3: shared memory box (Exp 7) ----------------

def api_sensors(body):
    lock = bool(body.get("lock", True))
    cmd = ["./exp7/buffer"] + ([] if lock else ["unsafe"])
    code, out = run(cmd)

    def num(label):
        m = re.search(label + r"\s*:\s*(\d+)", out)
        return int(m.group(1)) if m else None

    sent, first = {}, []
    path = os.path.join(ROOT, "logs/buffer_log.txt")
    if os.path.exists(path):
        for line in open(path):
            m = re.match(r"TX frame (\d+) from (\w+)", line)
            if m:
                no = int(m.group(1))
                sent[no] = sent.get(no, 0) + 1
                if len(first) < 10:
                    first.append({"frame": no, "sensor": m.group(2)})
    total = num("Frames transmitted") or 12
    twice = [n for n, c in sorted(sent.items()) if c > 1][:5]
    lost = [n for n in range(total) if n not in sent][:5]
    return {
        "ok": code == 0, "lock": lock, "command": " ".join(cmd), "output": out,
        "made": num("Frames created"), "total": total,
        "duplicates": num("Duplicate frames"), "lost": num("Lost frames"),
        "race": "RESULT: RACE" in out, "first": first, "twice": twice, "lost_examples": lost,
    }


# ---------------- Step 4: sharing power safely (Exp 9) ----------------

def vec_words(v):
    return ", ".join(f"{r} {x}" for r, x in zip(RESOURCES, v))


def api_power(body):
    try:
        task = int(body.get("task", 3))
        ask = [int(body.get(r, 0)) for r in RESOURCES]
    except (TypeError, ValueError):
        return {"ok": False, "error": "Please type whole numbers only."}
    if any(x < 0 for x in ask):
        return {"ok": False, "error": "A task cannot ask for a negative amount."}
    cmd = ["./exp9/bankers", str(task)] + [str(x) for x in ask]
    code, out = run(cmd)

    # starting table: who holds what / who still needs what
    table = []
    for i, name in enumerate(["P0_TTC", "P1_SSTV", "P2_VOICE", "P3_AI_ROUTER"]):
        m = re.search(rf"(?m)^{name}\s+((?:\d+\s+){{11}}\d+)", out)
        if m:
            n = [int(x) for x in m.group(1).split()]
            table.append({"task": TASKS[i], "has": n[4:8], "needs": n[8:12]})
    free = re.search(r"Available = \(([^)]*)\)", out)
    free = [int(x) for x in free.group(1).split(",")] if free else []

    # the check for THIS request (text after the REQUEST line)
    steps, verdict = [], "?"
    part = out.split("--- REQUEST:")[-1] if "--- REQUEST:" in out else ""
    names = dict(zip(["P0_TTC", "P1_SSTV", "P2_VOICE", "P3_AI_ROUTER"], TASKS))
    for line in part.splitlines():
        w = re.search(r"^\s*Work = \(([^)]*)\)", line)
        c = re.search(r"^\s*(\S+)\s+can finish.*Work = \(([^)]*)\)", line)
        if w:
            steps.append(ev(f"Pretend we say YES. Left free: {vec_words(w.group(1).split(', '))}", "info"))
        elif c:
            steps.append(ev(f"{names.get(c.group(1), c.group(1))} can finish with what is free, then gives "
                            f"everything back. Free now: {vec_words(c.group(2).split(', '))}", "ok"))
        elif "No other process can finish" in line:
            steps.append(ev("No task can finish with what is left. Everyone would wait for each other "
                            "forever (DEADLOCK)", "bad"))
        elif line.startswith("GRANT"):
            verdict = "GRANT"
        elif line.startswith("DENY"):
            verdict = "DENY"
        elif line.startswith("WAIT"):
            verdict = "WAIT"
            steps.append(ev("Not enough is free right now, so there is nothing to check yet", "warn"))
        elif line.startswith("ERROR"):
            verdict = "ERROR"
            steps.append(ev("The task asked for more than the maximum it said it would ever need", "bad"))
    return {"ok": code == 0, "command": " ".join(cmd), "output": out, "table": table,
            "free": free, "steps": steps, "verdict": verdict}


ROUTES = {
    ("POST", "/api/pass"): api_pass,
    ("GET", "/api/ai_jobs"): api_ai_jobs,
    ("POST", "/api/radio"): api_radio,
    ("POST", "/api/sensors"): api_sensors,
    ("POST", "/api/power"): api_power,
}


class Handler(BaseHTTPRequestHandler):
    def send(self, status, body, ctype="application/json"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def route(self, method):
        path = self.path.split("?")[0]
        if method == "GET" and path in ("/", "/index.html"):
            with open(os.path.join(ROOT, "web/index.html"), "rb") as f:
                return self.send(200, f.read(), "text/html; charset=utf-8")
        handler = ROUTES.get((method, path))
        if not handler:
            return self.send(404, {"error": "not found"})
        body = {}
        if method == "POST":
            length = int(self.headers.get("Content-Length", 0) or 0)
            if length:
                try:
                    body = json.loads(self.rfile.read(length))
                except json.JSONDecodeError:
                    return self.send(400, {"error": "bad JSON"})
        self.send(200, handler(body))

    def do_GET(self):
        self.route("GET")

    def do_POST(self):
        self.route("POST")

    def log_message(self, fmt, *args):
        print("[web]", self.command, self.path)


if __name__ == "__main__":
    os.makedirs(os.path.join(ROOT, "data"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "logs"), exist_ok=True)
    host = "127.0.0.1"
    print(f"SomaiyaSat Mission Simulator running -> open http://localhost:{PORT} in your browser")
    if "--any" in sys.argv:
        host = "0.0.0.0"
        ip = subprocess.run(["hostname", "-I"], capture_output=True, text=True).stdout.split()
        if ip:
            print(f"   or open http://{ip[0]}:{PORT}")
    print("Keep this window open. Press Ctrl+C to stop.")
    ThreadingHTTPServer((host, PORT), Handler).serve_forever()
