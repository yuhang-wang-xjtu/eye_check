"""SQLite online backup; run as the service account via the supplied timer."""
import os
import sqlite3
from datetime import datetime, timezone
from pathlib import Path

os.umask(0o077)
root = Path("/var/lib/eyebreak/backups")
root.mkdir(mode=0o700, exist_ok=True)
name = "session-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + ".sqlite"
with sqlite3.connect("/var/lib/eyebreak/session.sqlite") as source:
    with sqlite3.connect(root / name) as target:
        source.backup(target)
for old in sorted(root.glob("session-????????T??????Z.sqlite"), reverse=True)[14:]:
    old.unlink()
