"""Local reference implementation of the EyeBreak shared-session contract.

Run behind an HTTPS reverse proxy on the existing server. No third-party deps
on Linux; Windows may require tzdata for IANA time-zone data.
"""
import argparse
import copy
import hmac
import json
import re
import secrets
import sqlite3
import threading
import time
from datetime import datetime, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from zoneinfo import ZoneInfo

WORK_MS = 1_200_000
DEFAULTS = dict(restSeconds=25, sittingMinutes=60, workMinutes=120, nightHour=23,
                nightMinute=0, sittingEnabled=True, workEnabled=True, nightEnabled=True)


def new_session(now, zone="Asia/Shanghai"):
    ZoneInfo(zone)
    return dict(state="WORKING", updatedAtMs=now, deadlineMs=now + WORK_MS,
                remainingMs=WORK_MS, sittingMs=0, workMs=0, sittingSent=False,
                workSent=False, lastNight="", settings=DEFAULTS.copy(), usage={},
                revision=0, zone=zone)


def add_usage(s, start, end):
    zone = ZoneInfo(s["zone"])
    while start < end:
        local = datetime.fromtimestamp(start / 1000, zone)
        tomorrow = datetime.combine(local.date() + timedelta(days=1), datetime.min.time(), zone)
        stop = min(end, int(tomorrow.timestamp() * 1000))
        day = local.date().isoformat()
        s["usage"][day] = s["usage"].get(day, 0) + stop - start
        start = stop


def settle(s, now):
    previous = s["updatedAtMs"]
    events = []
    if now < previous:
        if s["state"] in ("WORKING", "RESTING"):
            s["deadlineMs"] = now + max(0, s["deadlineMs"] - previous)
        s["updatedAtMs"] = now
        return events
    old_state = s["state"]
    options = s["settings"]
    if old_state == "WORKING":
        end = min(now, s["deadlineMs"])
        elapsed = max(0, end - previous)
        s["sittingMs"] += elapsed
        s["workMs"] += elapsed
        add_usage(s, previous, end)
        if now >= s["deadlineMs"]:
            s["state"] = "RESTING"
            s["deadlineMs"] += options["restSeconds"] * 1000
            events.append("REST_START")
    elif old_state == "AWAY":
        recovery = min(now - previous, 365 * 86400000) * 5
        s["remainingMs"] = min(WORK_MS, s["remainingMs"] + recovery)
        for counter, flag, limit in (("sittingMs", "sittingSent", "sittingMinutes"),
                                     ("workMs", "workSent", "workMinutes")):
            s[counter] = max(0, s[counter] - recovery)
            if s[counter] < options[limit] * 60000:
                s[flag] = False
    if s["state"] == "RESTING" and now >= s["deadlineMs"]:
        s["state"] = "WAITING"
        s["remainingMs"] = WORK_MS
        events = ["REST_END"]
    s["updatedAtMs"] = now
    if old_state != s["state"]:
        s["revision"] += 1
    if s["state"] == "WORKING":
        for enabled, counter, limit, flag, event in (
            ("sittingEnabled", "sittingMs", "sittingMinutes", "sittingSent", "SITTING"),
            ("workEnabled", "workMs", "workMinutes", "workSent", "LONG_WORK")):
            if options[enabled] and not s[flag] and s[counter] >= options[limit] * 60000:
                s[flag] = True
                events.append(event)
        local = datetime.fromtimestamp(now / 1000, ZoneInfo(s["zone"]))
        minute = local.hour * 60 + local.minute
        start = options["nightHour"] * 60 + options["nightMinute"]
        night = (minute >= start or minute < 360) if start >= 360 else start <= minute < 360
        day = (local.date() - timedelta(days=1) if start >= 360 and minute < 360 else local.date()).isoformat()
        if options["nightEnabled"] and night and s["lastNight"] != day:
            s["lastNight"] = day
            events.append("LATE_NIGHT")
    return events


def validate_options(options):
    if not isinstance(options, dict) or set(options) != set(DEFAULTS):
        raise ValueError("settings must contain exactly the shared reminder fields")
    for key, default in DEFAULTS.items():
        if type(options[key]) is not type(default):
            raise ValueError("invalid setting type")
    for key, low, high in (("restSeconds", 1, 600), ("sittingMinutes", 1, 480),
                           ("workMinutes", 1, 720), ("nightHour", 0, 23), ("nightMinute", 0, 59)):
        if not low <= options[key] <= high:
            raise ValueError("setting out of range")


def apply_command(s, action, now, options=None):
    state = s["state"]
    if action == "pause" and state == "WORKING":
        s["remainingMs"] = max(0, s["deadlineMs"] - now)
        s["state"] = "PAUSED"
    elif action == "continue" and state in ("PAUSED", "WAITING", "AWAY"):
        s["deadlineMs"] = now + s["remainingMs"]
        s["state"] = "WORKING"
    elif action == "rest" and state != "AWAY":
        if state == "WORKING":
            s["remainingMs"] = max(0, s["deadlineMs"] - now)
        elif state != "PAUSED":
            s["remainingMs"] = WORK_MS
        s["state"] = "AWAY"
    elif action == "skip" and state == "RESTING":
        s["state"] = "WORKING"
        s["deadlineMs"] = now + WORK_MS
        s["remainingMs"] = WORK_MS
    elif action == "stood_up":
        s["sittingMs"], s["sittingSent"] = 0, False
    elif action == "end_work":
        s.update(sittingMs=0, workMs=0, sittingSent=False, workSent=False,
                 remainingMs=WORK_MS, state="PAUSED")
    elif action == "settings":
        validate_options(options)
        s["settings"] = options.copy()
    elif action not in ("pause", "continue", "rest", "skip"):
        raise ValueError("unknown action")
    else:
        raise ValueError("action is not available in the current state")
    s["updatedAtMs"] = now
    s["revision"] += 1


class Store:
    def __init__(self, path, zone="Asia/Shanghai", clock=None):
        ZoneInfo(zone)
        self.zone, self.clock = zone, clock or (lambda: int(time.time() * 1000))
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("CREATE TABLE IF NOT EXISTS sessions (id TEXT PRIMARY KEY, state TEXT NOT NULL)")
        self.db.execute("CREATE TABLE IF NOT EXISTS operations (room TEXT, id TEXT, fingerprint TEXT, PRIMARY KEY(room,id))")
        self.db.commit()
        self.lock = threading.Lock()

    def request(self, room, command=None):
        if not re.fullmatch(r"[a-zA-Z0-9_-]{8,80}", room):
            raise ValueError("invalid session identifier")
        with self.lock, self.db:
            now = self.clock()
            row = self.db.execute("SELECT state FROM sessions WHERE id=?", (room,)).fetchone()
            s = json.loads(row[0]) if row else new_session(now, self.zone)
            events = settle(s, now)
            status = 200
            if command is not None:
                if not isinstance(command, dict) or type(command.get("expectedRevision")) is not int:
                    raise ValueError("expectedRevision required")
                operation = command.get("operationId", "")
                if not isinstance(operation, str) or not re.fullmatch(r"[a-zA-Z0-9_-]{8,100}", operation):
                    raise ValueError("invalid operation identifier")
                fingerprint = json.dumps(command, sort_keys=True, separators=(",", ":"))
                prior = self.db.execute("SELECT fingerprint FROM operations WHERE room=? AND id=?", (room, operation)).fetchone()
                if prior:
                    if prior[0] != fingerprint:
                        raise ValueError("operation identifier reused for a different request")
                elif command["expectedRevision"] != s["revision"]:
                    status = 409
                else:
                    apply_command(s, command.get("action"), now, command.get("settings"))
                    self.db.execute("INSERT INTO operations VALUES (?,?,?)", (room, operation, fingerprint))
            self.db.execute("INSERT OR REPLACE INTO sessions VALUES (?,?)", (room, json.dumps(s)))
            return status, dict(serverNowMs=now, snapshot=copy.deepcopy(s), events=events)


def handler(store, token):
    class Handler(BaseHTTPRequestHandler):
        server_version = "EyeBreakSync/0.1"
        def log_message(self, *_):
            pass  # No tokens, session ids, or usage records in HTTP logs.

        def reply(self, code, body):
            raw = json.dumps(body, ensure_ascii=False).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(raw)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(raw)

        def handle_request(self, post):
            self.connection.settimeout(10)
            auth = self.headers.get("Authorization", "")
            if not hmac.compare_digest(auth.encode(), ("Bearer " + token).encode()):
                self.reply(401, {"error": "unauthorized"}); return
            pattern = r"/v1/sessions/([a-zA-Z0-9_-]{8,80})" + (r"/commands" if post else "")
            matched = re.fullmatch(pattern, self.path)
            if not matched:
                self.reply(404, {"error": "not found"}); return
            try:
                command = None
                if post:
                    size = int(self.headers.get("Content-Length", "0"))
                    if not 0 < size <= 65536:
                        raise ValueError("request body must be 1–65536 bytes")
                    command = json.loads(self.rfile.read(size))
                code, body = store.request(matched[1], command)
                self.reply(code, body)
            except (ValueError, KeyError, TypeError, OverflowError):
                self.reply(400, {"error": "invalid request or action not available"})

        def do_GET(self): self.handle_request(False)
        def do_POST(self): self.handle_request(True)
    return Handler


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--database", default="sync/session.sqlite")
    parser.add_argument("--token-file", default="sync/.token")
    parser.add_argument("--timezone", default="Asia/Shanghai")
    parser.add_argument("--init-token", action="store_true", help="Create token file without printing its value")
    args = parser.parse_args()
    path = Path(args.token_file)
    if args.init_token:
        with path.open("x", encoding="utf-8") as file:
            file.write(secrets.token_urlsafe(32))
        try: path.chmod(0o600)
        except OSError: pass
        print("Token file created; configure both clients with its contents.")
        return
    token = path.read_text(encoding="utf-8").strip()
    if len(token) < 16:
        parser.error("token must be at least 16 characters")
    store = Store(args.database, args.timezone)
    server = ThreadingHTTPServer((args.bind, args.port), handler(store, token))
    print(f"EyeBreak contract server listening on {args.bind}:{args.port}; HTTPS reverse proxy required remotely.")
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally: server.server_close(); store.db.close()


if __name__ == "__main__": main()
