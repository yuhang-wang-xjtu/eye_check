import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest
import urllib.request
import urllib.error
from http.server import ThreadingHTTPServer

spec = importlib.util.spec_from_file_location("server", Path(__file__).with_name("server.py"))
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)


class Tests(unittest.TestCase):
    def setUp(self):
        self.now = 1_700_000_000_000
        self.store = server.Store(":memory:", clock=lambda: self.now)
        self.room = "shared-session-01"
        self.operation = 0
    def tearDown(self): self.store.db.close()
    def fetch(self): return self.store.request(self.room)[1]["snapshot"]
    def command(self, action, revision=None):
        self.operation += 1
        revision = self.fetch()["revision"] if revision is None else revision
        return self.store.request(self.room, dict(action=action, expectedRevision=revision, operationId=f"operation-{self.operation:08}"))
    def test_two_devices_do_not_double_count(self):
        self.fetch(); self.now += 60000
        a = self.fetch(); b = self.fetch()
        self.assertEqual(sum(a["usage"].values()), 60000)
        self.assertEqual(a["usage"], b["usage"])
    def test_pause_recovery_and_usage(self):
        self.fetch(); self.now += 148000
        self.command("pause"); self.now += 2000
        self.command("rest"); self.now += 1000
        s = self.fetch()
        self.assertEqual(s["remainingMs"], 1057000)
        self.assertEqual(s["workMs"], 143000)
        self.assertEqual(sum(s["usage"].values()), 148000)
        self.command("end_work")
        self.assertEqual(sum(self.fetch()["usage"].values()), 148000)
    def test_rest_finishes_waiting_forever_without_new_command(self):
        self.fetch(); self.now += server.WORK_MS + 25000
        self.assertEqual(self.fetch()["state"], "WAITING")
        self.now += 3 * 86400000
        s = self.fetch()
        self.assertEqual(s["state"], "WAITING")
        self.assertEqual(sum(s["usage"].values()), server.WORK_MS)
        self.command("continue")
        self.assertEqual(self.fetch()["state"], "WORKING")
    def test_revision_conflict_and_idempotency(self):
        revision = self.fetch()["revision"]
        self.command("pause", revision)
        self.assertEqual(self.command("rest", revision)[0], 409)
        revision = self.fetch()["revision"]
        command = dict(action="rest", expectedRevision=revision, operationId="retry-operation-01")
        a = self.store.request(self.room, command)
        b = self.store.request(self.room, command)
        self.assertEqual(a, b)
        self.assertEqual(a[1]["snapshot"]["state"], "AWAY")
    def test_midnight(self):
        s = server.new_session(0, "Asia/Shanghai")
        # 2026-10-19 23:59:55 +08:00 through the next 10 seconds.
        start = int(server.datetime(2026, 10, 19, 23, 59, 55, tzinfo=server.ZoneInfo("Asia/Shanghai")).timestamp()*1000)
        server.add_usage(s, start, start+10000)
        self.assertEqual(s["usage"], {"2026-10-19":5000,"2026-10-20":5000})
    def test_persistence(self):
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory)/"session.sqlite")
            a = server.Store(path, clock=lambda: self.now)
            a.request(self.room); self.now += 1000
            expected = a.request(self.room)[1]
            a.db.close()
            b = server.Store(path, clock=lambda: self.now)
            self.assertEqual(b.request(self.room)[1], expected)
            b.db.close()
    def test_invalid_settings_and_invalid_actions(self):
        s = self.fetch()
        with self.assertRaises(ValueError): server.apply_command(s, "settings", self.now, {"restSeconds":-1})
        with self.assertRaises(ValueError): server.apply_command(s, "continue", self.now)
    def test_http_auth_and_contract(self):
        http = ThreadingHTTPServer(("127.0.0.1",0),server.handler(self.store,"a-test-token-with-32-characters"))
        worker = threading.Thread(target=http.serve_forever,daemon=True); worker.start()
        url=f"http://127.0.0.1:{http.server_port}/v1/sessions/{self.room}"
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{http.server_port}/health") as health:
                self.assertEqual(json.load(health), {"status":"ok", "protocol":1})
            with self.assertRaises(urllib.error.HTTPError) as denied: urllib.request.urlopen(url)
            self.assertEqual(denied.exception.code,401)
            request=urllib.request.Request(url,headers={"Authorization":"Bearer a-test-token-with-32-characters"})
            with urllib.request.urlopen(request) as response:
                body=json.load(response)
                self.assertEqual(body["snapshot"]["state"],"WORKING")
                self.assertIn("serverNowMs",body)
        finally:
            http.shutdown();http.server_close();worker.join()


class DeviceTests(unittest.TestCase):
    def setUp(self):
        self.now = 1800000000000
        self.store = server.Store(":memory:", clock=lambda: self.now)
        self.room, self.pc, self.phone = "device-test-room", "windows-device-01", "android-device-01"
        self.seq = 0
    def tearDown(self): self.store.db.close()
    def get(self, device): return self.store.device_request(self.room, device)[1]["snapshot"]
    def cmd(self, device, action, **extra):
        self.seq += 1
        command = dict(action=action, expectedRevision=self.get(device)["revision"], operationId=f"device-op-{self.seq:08}", **extra)
        return self.store.device_request(self.room, device, command)[1]["snapshot"]
    def observe(self, device, available):
        return self.cmd(device, "observe", available=available, platform="android" if device==self.phone else "windows")
    def work(self, device):
        self.observe(device, True)
        return self.cmd(device, "continue")
    def test_phone_never_resumes_pc(self):
        self.work(self.pc); self.cmd(self.pc, "rest")
        self.work(self.phone)
        self.assertEqual(self.get(self.pc)["state"], "AWAY")
        self.now += 10000
        self.observe(self.phone, False)
        self.assertEqual(self.get(self.phone)["state"], "AWAY")
        self.assertEqual(self.get(self.pc)["state"], "AWAY")
        self.now += 10000
        self.assertEqual(self.get(self.phone)["workMs"], 0)
        self.assertEqual(self.get(self.pc)["workMs"], 0)
    def test_pc_activity_never_starts_off_phone(self):
        self.work(self.phone); self.observe(self.phone, False)
        self.work(self.pc)
        self.assertEqual(self.get(self.phone)["state"], "AWAY")
        self.observe(self.pc, False); self.observe(self.pc, True)
        self.assertEqual(self.get(self.pc)["state"], "AWAY")
    def test_overlapping_usage_is_union(self):
        self.work(self.pc); self.work(self.phone)
        self.now += 10000; self.observe(self.phone, False)
        self.now += 5000; self.cmd(self.pc, "rest")
        pc, phone = self.get(self.pc), self.get(self.phone)
        self.assertEqual(sum(pc["usage"].values()), 15000)
        self.assertEqual(sum(pc["deviceUsage"].values()), 15000)
        self.assertEqual(sum(phone["deviceUsage"].values()), 10000)
        self.assertEqual(pc["usage"], phone["usage"])
    def test_disconnected_device_lease_expires(self):
        self.work(self.phone)
        self.now += 60000
        s = self.get(self.phone)
        self.assertEqual(s["state"], "AWAY")
        self.assertEqual(sum(s["usage"].values()), server.LEASE_MS)
        self.observe(self.phone, True)
        self.assertEqual(self.get(self.phone)["state"], "AWAY")
    def test_settings_shared_but_commands_independent(self):
        self.work(self.pc); self.work(self.phone)
        options = server.DEFAULTS.copy(); options["restSeconds"] = 37
        self.cmd(self.pc, "settings", settings=options)
        self.assertEqual(self.get(self.phone)["settings"]["restSeconds"], 37)
        self.cmd(self.phone, "pause")
        self.assertEqual(self.get(self.pc)["state"], "WORKING")
    def test_off_phone_finishes_rest_without_work(self):
        self.work(self.phone)
        # Seed a near deadline rather than waiting twenty real minutes.
        with self.store.db:
            s = self.get(self.phone); s["usage"] = s.pop("deviceUsage")
            s["deadlineMs"] = self.now+1000; s["settings"]["restSeconds"] = 2
            self.store.db.execute("UPDATE device_sessions SET state=? WHERE room=? AND device=?",(json.dumps(s), self.room,self.phone))
        self.now += 1000
        self.assertEqual(self.get(self.phone)["state"], "RESTING")
        self.observe(self.phone, False); self.now += 3000
        self.assertEqual(self.get(self.phone)["state"], "AWAY")
        self.assertEqual(sum(self.get(self.phone)["deviceUsage"].values()), 1000)
    def test_legacy_state_does_not_control_new_devices(self):
        self.store.request(self.room)
        self.assertEqual(self.get(self.pc)["state"], "WAITING")
        self.store.request(self.room,dict(action="pause",expectedRevision=0,operationId="legacy-pause-001"))
        self.work(self.phone)
        self.assertEqual(self.get(self.pc)["state"], "WAITING")

if __name__=="__main__": unittest.main()
