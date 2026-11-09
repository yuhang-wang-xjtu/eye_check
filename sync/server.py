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
LEASE_MS = 30000
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
        self.db.execute("CREATE TABLE IF NOT EXISTS device_sessions (room TEXT, device TEXT, state TEXT NOT NULL, PRIMARY KEY(room,device))")
        self.db.execute("CREATE TABLE IF NOT EXISTS device_operations (room TEXT, device TEXT, id TEXT, fingerprint TEXT, created INTEGER NOT NULL, PRIMARY KEY(room,device,id))")
        self.db.execute("CREATE INDEX IF NOT EXISTS device_operation_age ON device_operations(room,device,created)")
        self.db.execute("CREATE TABLE IF NOT EXISTS shared_rooms (room TEXT PRIMARY KEY, settings TEXT NOT NULL, legacy TEXT NOT NULL)")
        self.db.execute("CREATE TABLE IF NOT EXISTS usage_intervals (room TEXT, day TEXT, intervals TEXT NOT NULL, PRIMARY KEY(room,day))")
        self.db.execute("CREATE TABLE IF NOT EXISTS coordinated_sessions (room TEXT PRIMARY KEY, state TEXT NOT NULL)")
        self.db.execute("CREATE TABLE IF NOT EXISTS coordinated_operations (room TEXT, device TEXT, id TEXT, fingerprint TEXT, created INTEGER NOT NULL, kind TEXT NOT NULL, PRIMARY KEY(room,device,id))")
        self.db.execute("CREATE INDEX IF NOT EXISTS coordinated_operation_age ON coordinated_operations(room,device,created)")
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

    def record_interval(self, room, start, end):
        """Union of confirmed device work, split by day, never summed twice."""
        zone = ZoneInfo(self.zone)
        while start < end:
            local = datetime.fromtimestamp(start / 1000, zone)
            midnight = datetime.combine(local.date() + timedelta(days=1), datetime.min.time(), zone)
            stop = min(end, int(midnight.timestamp() * 1000))
            day = local.date().isoformat()
            row = self.db.execute("SELECT intervals FROM usage_intervals WHERE room=? AND day=?", (room, day)).fetchone()
            ranges = json.loads(row[0]) if row else []
            ranges.append([start, stop])
            merged = []
            for left, right in sorted(ranges):
                if merged and left <= merged[-1][1]:
                    merged[-1][1] = max(merged[-1][1], right)
                else:
                    merged.append([left, right])
            self.db.execute("INSERT OR REPLACE INTO usage_intervals VALUES (?,?,?)", (room, day, json.dumps(merged)))
            start = stop

    def settle_device(self, room, s, now):
        previous = s["updatedAtMs"]
        cutoff = max(previous, min(now, s["leaseUntilMs"])) if s["available"] else previous
        if s["state"] == "WORKING" and s["available"]:
            self.record_interval(room, previous, min(cutoff, s["deadlineMs"]))
        events = settle(s, cutoff)
        # Expired availability never proves that the user has returned.
        if s["available"] and now >= s["leaseUntilMs"]:
            s["available"] = False
            if s["state"] == "WORKING":
                apply_command(s, "rest", cutoff)
        if not s["available"]:
            if s["state"] == "RESTING":
                recovery = max(0, min(now, s["deadlineMs"]) - s["updatedAtMs"]) * 5
                s["sittingMs"] = max(0, s["sittingMs"] - recovery)
                s["workMs"] = max(0, s["workMs"] - recovery)
            events += settle(s, now)
            if s["platform"] == "android" and s["state"] == "WAITING":
                apply_command(s, "rest", now)
        elif cutoff < now:
            events += settle(s, now)
        return list(dict.fromkeys(events))

    def device_request(self, room, device, command=None):
        if not re.fullmatch(r"[a-zA-Z0-9_-]{8,80}", room) or not re.fullmatch(r"[a-zA-Z0-9_-]{8,80}", device):
            raise ValueError("invalid identity")
        with self.lock, self.db:
            now = self.clock()
            shared = self.db.execute("SELECT settings,legacy FROM shared_rooms WHERE room=?", (room,)).fetchone()
            if not shared:
                old = self.db.execute("SELECT state FROM sessions WHERE id=?", (room,)).fetchone()
                old = json.loads(old[0]) if old else None
                options, legacy = (old["settings"], old["usage"]) if old else (DEFAULTS.copy(), {})
                self.db.execute("INSERT INTO shared_rooms VALUES (?,?,?)", (room, json.dumps(options), json.dumps(legacy)))
            else:
                options, legacy = json.loads(shared[0]), json.loads(shared[1])
            rows = self.db.execute("SELECT device,state FROM device_sessions WHERE room=?", (room,)).fetchall()
            states = {key: json.loads(value) for key, value in rows}
            if device not in states:
                if len(states) >= 32:
                    raise ValueError("too many devices")
                s = new_session(now, self.zone)
                s.update(state="WAITING", settings=options.copy(), available=False, leaseUntilMs=now, platform="windows")
                states[device] = s
            events = []
            for key, value in states.items():
                due = self.settle_device(room, value, now)
                if key == device:
                    events = due
            s = states[device]
            status = 200
            if command is not None:
                if not isinstance(command, dict) or type(command.get("expectedRevision")) is not int:
                    raise ValueError("expectedRevision required")
                operation = command.get("operationId", "")
                if not isinstance(operation, str) or not re.fullmatch(r"[a-zA-Z0-9_-]{8,100}", operation):
                    raise ValueError("invalid operation")
                fingerprint = json.dumps(command, sort_keys=True, separators=(",", ":"))
                prior = self.db.execute("SELECT fingerprint FROM device_operations WHERE room=? AND device=? AND id=?", (room, device, operation)).fetchone()
                action = command.get("action")
                if prior:
                    if prior[0] != fingerprint:
                        raise ValueError("operation reused")
                elif action != "observe" and command["expectedRevision"] != s["revision"]:
                    status = 409
                else:
                    if action == "observe":
                        available, platform = command.get("available"), command.get("platform")
                        if type(available) is not bool or platform not in ("windows", "android"):
                            raise ValueError("invalid observation")
                        s["platform"] = platform
                        if not available and (s["state"] == "WORKING" or (platform == "android" and s["state"] in ("PAUSED", "WAITING"))):
                            apply_command(s, "rest", now)
                        s["available"] = available
                        s["leaseUntilMs"] = now + LEASE_MS if available else now
                    elif action == "settings":
                        validate_options(command.get("settings"))
                        options = command["settings"]
                        for value in states.values():
                            apply_command(value, "settings", now, options)
                        self.db.execute("UPDATE shared_rooms SET settings=? WHERE room=?", (json.dumps(options), room))
                    else:
                        if not s["available"] and action in ("continue", "skip"):
                            raise ValueError("inactive device cannot work")
                        apply_command(s, action, now)
                    self.db.execute("INSERT INTO device_operations VALUES (?,?,?,?,?)", (room, device, operation, fingerprint, now))
            self.db.execute("DELETE FROM device_operations WHERE room=? AND device=? AND created<?", (room, device, now-86400000))
            for key, value in states.items():
                self.db.execute("INSERT OR REPLACE INTO device_sessions VALUES (?,?,?)", (room, key, json.dumps(value)))
            usage = legacy.copy()
            for day, intervals in self.db.execute("SELECT day,intervals FROM usage_intervals WHERE room=?", (room,)):
                usage[day] = min(86400000, usage.get(day, 0) + sum(end - start for start, end in json.loads(intervals)))
            snapshot = copy.deepcopy(s)
            snapshot["deviceUsage"] = snapshot["usage"]
            snapshot["usage"] = usage
            return status, dict(protocol=2, serverNowMs=now, snapshot=snapshot, events=events)


    @staticmethod
    def coordinate(s, now):
        """Reconcile the shared clock without copying another device's mode."""
        if s["state"] == "RESTING":
            return
        active = any(d["active"] and d["available"] and d["leaseUntilMs"] > now for d in s["devices"].values())
        desired = "WORKING" if active else "AWAY"
        if s["state"] != desired:
            if desired == "AWAY":
                s["remainingMs"] = max(0, s["deadlineMs"] - now) if s["state"] == "WORKING" else s["remainingMs"]
            else:
                s["deadlineMs"] = now + s["remainingMs"]
            s["state"] = desired
            s["revision"] += 1

    def settle_coordinated(self, s, now):
        events = []
        if now < s["updatedAtMs"]:
            delta = now - s["updatedAtMs"]
            for d in s["devices"].values():
                d["leaseUntilMs"] += delta
            return settle(s, now)
        # Stop exactly at expiries and timer boundaries; delayed polling must
        # not count a vanished device until the end of the entire request gap.
        while True:
            cursor = s["updatedAtMs"]
            boundaries = [now]
            boundaries += [d["leaseUntilMs"] for d in s["devices"].values() if d["available"] and d["leaseUntilMs"] >= cursor]
            if s["state"] in ("WORKING", "RESTING"):
                boundaries.append(max(cursor, s["deadlineMs"]))
            at = min(boundaries)
            before = s["state"]
            events += settle(s, at)
            for d in s["devices"].values():
                if d["available"] and d["leaseUntilMs"] <= at:
                    d.update(available=False, active=False, mode="RESTING" if d["mode"] == "RESTING" else "AWAY", revision=d["revision"]+1)
            if before == "WORKING" and s["state"] == "RESTING":
                s["restCycle"] += 1
                for d in s["devices"].values():
                    if d["active"]:
                        d.update(active=False, mode="RESTING", revision=d["revision"]+1)
            if before == "RESTING" and s["state"] == "WAITING":
                s["restFinished"] = s["restCycle"]
                for d in s["devices"].values():
                    if d["platform"] == "android" and d["available"]:
                        d.update(active=True, mode="WORKING", revision=d["revision"]+1)
                    elif d["mode"] == "RESTING":
                        d.update(active=False, mode="WAITING" if d["available"] else "AWAY", revision=d["revision"]+1)
            self.coordinate(s, at)
            if at >= now:
                break
        return list(dict.fromkeys(events))

    def coordinated_request(self, room, device, command=None):
        if not re.fullmatch(r"[a-zA-Z0-9_-]{8,80}", room) or not re.fullmatch(r"[a-zA-Z0-9_-]{8,80}", device):
            raise ValueError("invalid identity")
        with self.lock, self.db:
            now = self.clock()
            row = self.db.execute("SELECT state FROM coordinated_sessions WHERE room=?", (room,)).fetchone()
            if row:
                s = json.loads(row[0])
            else:
                s = new_session(now, self.zone)
                s.update(state="AWAY", devices={}, restCycle=0, restFinished=0)
                # Freeze existing history once. Old V1/V2 activity never drives V3.
                legacy = self.db.execute("SELECT settings,legacy FROM shared_rooms WHERE room=?", (room,)).fetchone()
                if legacy:
                    s["settings"], s["usage"] = json.loads(legacy[0]), json.loads(legacy[1])
                    for day, intervals in self.db.execute("SELECT day,intervals FROM usage_intervals WHERE room=?", (room,)):
                        s["usage"][day] = min(86400000, s["usage"].get(day, 0)+sum(b-a for a,b in json.loads(intervals)))
                else:
                    old = self.db.execute("SELECT state FROM sessions WHERE id=?", (room,)).fetchone()
                    if old:
                        old = json.loads(old[0]); s["settings"], s["usage"] = old["settings"], old["usage"]
            events = self.settle_coordinated(s, now)
            if device not in s["devices"]:
                if len(s["devices"]) >= 32:
                    raise ValueError("too many devices")
                s["devices"][device] = dict(available=False, active=False, mode="WAITING", leaseUntilMs=now, platform="windows", revision=0)
            d = s["devices"][device]
            status = 200
            if command is not None:
                if not isinstance(command, dict) or type(command.get("expectedRevision")) is not int:
                    raise ValueError("expectedRevision required")
                operation = command.get("operationId", "")
                if not isinstance(operation, str) or not re.fullmatch(r"[a-zA-Z0-9_-]{8,100}", operation):
                    raise ValueError("invalid operation")
                fingerprint = json.dumps(command, sort_keys=True, separators=(",", ":"))
                prior = self.db.execute("SELECT fingerprint FROM coordinated_operations WHERE room=? AND device=? AND id=?", (room,device,operation)).fetchone()
                action = command.get("action")
                local_action = action in ("continue", "rest", "pause", "end_work")
                expected = command.get("expectedDeviceRevision", command["expectedRevision"]) if local_action else command["expectedRevision"]
                current = d["revision"] if local_action and "expectedDeviceRevision" in command else s["revision"]
                if prior:
                    if prior[0] != fingerprint:
                        raise ValueError("operation reused")
                elif action != "observe" and (type(expected) is not int or expected != current):
                    status = 409
                elif action == "continue" and s["state"] == "RESTING":
                    status = 409  # stale input cannot skip a timed automatic rest
                else:
                    if action == "observe":
                        available, platform, resume = command.get("available"), command.get("platform"), command.get("resume",False)
                        if type(available) is not bool or type(resume) is not bool or platform not in ("windows","android"):
                            raise ValueError("invalid observation")
                        was_available = d["available"]
                        d["platform"], d["available"] = platform, available
                        d["leaseUntilMs"] = now+LEASE_MS if available else now
                        if not available:
                            mode = "RESTING" if s["state"] == "RESTING" and d["mode"] == "RESTING" else "AWAY"
                            if d["active"] or d["mode"] != mode:
                                d.update(active=False,mode=mode,revision=d["revision"]+1)
                        elif platform == "android" and (resume or not was_available) and s["state"] != "RESTING":
                            d.update(active=True,mode="WORKING",revision=d["revision"]+1)
                    elif action == "continue":
                        if not d["available"]:
                            raise ValueError("inactive device cannot work")
                        d.update(active=True,mode="WORKING",revision=d["revision"]+1)
                    elif action in ("rest","pause","end_work"):
                        d.update(active=False,mode="AWAY",revision=d["revision"]+1)
                        if action == "end_work" and not any(x["active"] for x in s["devices"].values()):
                            s.update(sittingMs=0,workMs=0,sittingSent=False,workSent=False)
                    elif action == "settings":
                        validate_options(command.get("settings")); s["settings"] = command["settings"].copy(); s["revision"] += 1
                    elif action == "stood_up":
                        s.update(sittingMs=0,sittingSent=False,revision=s["revision"]+1)
                    elif action == "skip" and s["state"] == "RESTING" and d["available"]:
                        s.update(state="AWAY",remainingMs=WORK_MS,revision=s["revision"]+1)
                        for x in s["devices"].values():
                            if x["mode"] == "RESTING":
                                x.update(mode="WAITING",active=False,revision=x["revision"]+1)
                        d.update(mode="WORKING",active=True,revision=d["revision"]+1)
                    else:
                        raise ValueError("invalid action")
                    self.coordinate(s, now)
                    self.db.execute("INSERT INTO coordinated_operations VALUES (?,?,?,?,?,?)", (room,device,operation,fingerprint,now,action))
            self.db.execute("DELETE FROM coordinated_operations WHERE room=? AND device=? AND (created<? OR (kind='observe' AND created<?))", (room,device,now-86400000,now-300000))
            self.db.execute("INSERT OR REPLACE INTO coordinated_sessions VALUES (?,?)", (room,json.dumps(s)))
            snapshot = {k:copy.deepcopy(v) for k,v in s.items() if k != "devices"}
            snapshot.update(available=d["available"],deviceActive=d["active"],deviceState=d["mode"],deviceRevision=d["revision"],
                            otherActive=any(x["active"] for k,x in s["devices"].items() if k!=device),activeDevices=sum(x["active"] for x in s["devices"].values()))
            return status,dict(protocol=3,serverNowMs=now,snapshot=snapshot,events=events)


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
            if not post and self.path == "/health":
                self.reply(200, {"status": "ok", "protocol": 1}); return
            auth = self.headers.get("Authorization", "")
            if not hmac.compare_digest(auth.encode(), ("Bearer " + token).encode()):
                self.reply(401, {"error": "unauthorized"}); return
            pattern = r"/v1/sessions/([a-zA-Z0-9_-]{8,80})" + (r"/commands" if post else "")
            matched = re.fullmatch(pattern, self.path)
            device_pattern = r"/v2/sessions/([a-zA-Z0-9_-]{8,80})/devices/([a-zA-Z0-9_-]{8,80})" + (r"/commands" if post else "")
            device_match = re.fullmatch(device_pattern, self.path)
            coordinated_pattern = device_pattern.replace("/v2/", "/v3/")
            coordinated_match = re.fullmatch(coordinated_pattern, self.path)
            if not matched and not device_match and not coordinated_match:
                self.reply(404, {"error": "not found"}); return
            try:
                command = None
                if post:
                    size = int(self.headers.get("Content-Length", "0"))
                    if not 0 < size <= 65536:
                        raise ValueError("request body must be 1–65536 bytes")
                    command = json.loads(self.rfile.read(size))
                if coordinated_match:
                    code, body = store.coordinated_request(coordinated_match[1],coordinated_match[2],command)
                else:
                    code, body = store.device_request(device_match[1], device_match[2], command) if device_match else store.request(matched[1], command)
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
