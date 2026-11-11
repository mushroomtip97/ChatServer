"""
Password hashing test suite.

Verifies:
1. Wrong password is rejected (no auth bypass)
2. New registrations produce Argon2id hashes in the DB
3. Legacy SHA-256 users get transparently migrated to Argon2id on login

Assumes the server is running on wss://localhost:8080 with the standard
self-signed cert at certs/server.crt.
"""

import asyncio
import json
import sqlite3
import ssl
import sys
import time
import uuid

import websockets


SERVER_URI = "wss://localhost:8080"
CERT_PATH = r"C:\Users\PC\QtProjects\chat-server-project\chat-server\certs\server.crt"
DB_PATH   = r"C:\Users\PC\QtProjects\chat-server-project\build-debug\chat_server.db"
TIMEOUT   = 5.0


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def ssl_ctx():
    ctx = ssl.create_default_context()
    ctx.load_verify_locations(CERT_PATH)
    return ctx


async def send_recv(ws, obj, timeout=TIMEOUT):
    """Send a JSON message and return the next parsed frame."""
    await ws.send(json.dumps(obj))
    raw = await asyncio.wait_for(ws.recv(), timeout=timeout)
    return json.loads(raw)


def db_query(sql, params=()):
    """Run a read query and return all rows."""
    conn = sqlite3.connect(DB_PATH)
    try:
        cur = conn.cursor()
        cur.execute(sql, params)
        return cur.fetchall()
    finally:
        conn.close()


def get_hash(username):
    rows = db_query(
        "SELECT password_hash FROM users WHERE username = ?", (username,)
    )
    return rows[0][0] if rows else None


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

async def t1_wrong_password_rejected():
    """A wrong password must NOT return auth_success."""
    async with websockets.connect(SERVER_URI, ssl=ssl_ctx()) as ws:
        u = f"pwtest_{uuid.uuid4().hex[:8]}"
        pw = "correct123"

        reg = await send_recv(ws, {
            "type": "register",
            "data": {"username": u, "password": pw},
        })
        assert reg["type"] == "register_success", reg

        auth = await send_recv(ws, {
            "type": "auth",
            "data": {"username": u, "password": "WRONG_password"},
        })
        assert auth["type"] == "error", \
            f"BUG: wrong password accepted! got {auth}"
        assert auth["data"]["code"] == 401, auth

        # Sanity: correct password still works
        auth = await send_recv(ws, {
            "type": "auth",
            "data": {"username": u, "password": pw},
        })
        assert auth["type"] == "auth_success", auth

        print("    ✓ wrong password rejected, correct password accepted")


async def t2_new_users_use_argon2():
    """Fresh registrations must store an Argon2id hash."""
    async with websockets.connect(SERVER_URI, ssl=ssl_ctx()) as ws:
        u = f"argon_{uuid.uuid4().hex[:8]}"
        pw = "testpass123"

        reg = await send_recv(ws, {
            "type": "register",
            "data": {"username": u, "password": pw},
        })
        assert reg["type"] == "register_success", reg

        # Small delay so the commit lands
        await asyncio.sleep(0.2)

        stored = get_hash(u)
        assert stored is not None, f"no hash row for {u}"
        assert stored.startswith("$argon2id$"), \
            f"expected Argon2id hash, got: {stored[:40]}"
        assert "$v=19$" in stored, f"unexpected Argon2 version: {stored[:40]}"

        print(f"    ✓ new user hash: {stored[:40]}...")


async def t3_legacy_hash_migrates_on_login():
    """A user with a legacy SHA-256 hash should be upgraded on successful login."""
    # Find a user whose hash is still legacy SHA-256 (64 hex chars, no $argon2)
    rows = db_query("""
        SELECT username FROM users
        WHERE password_hash NOT LIKE '$argon2%'
        LIMIT 1
    """)

    if not rows:
        print("    ⚠ no legacy-hash user found (nothing to migrate)")
        print("      (this is fine if all users have already been migrated)")
        return

    username = rows[0][0]
    before = get_hash(username)

    # We need the correct password to log in and trigger migration.
    # `demo` is the only user we know the password for (demo123).
    if username != "demo":
        print(f"    ⚠ legacy user is '{username}' — password unknown, skipping")
        print("      (only 'demo' has a known password)")
        return

    async with websockets.connect(SERVER_URI, ssl=ssl_ctx()) as ws:
        auth = await send_recv(ws, {
            "type": "auth",
            "data": {"username": "demo", "password": "demo123"},
        })
        assert auth["type"] == "auth_success", auth

    # Give the migration UPDATE time to commit
    await asyncio.sleep(0.3)

    after = get_hash(username)
    assert after.startswith("$argon2id$"), \
        f"migration did not fire — hash still: {after[:40]}"
    assert after != before, "hash unchanged"

    print(f"    ✓ migrated {username}:")
    print(f"        before: {before[:40]}...")
    print(f"        after:  {after[:40]}...")


async def t4_argon2_verify_still_works():
    """After migration, the user can still log in with the same password."""
    async with websockets.connect(SERVER_URI, ssl=ssl_ctx()) as ws:
        auth = await send_recv(ws, {
            "type": "auth",
            "data": {"username": "demo", "password": "demo123"},
        })
        assert auth["type"] == "auth_success", auth
        print("    ✓ post-migration login works")


async def t5_argon2_verify_rejects_wrong_password():
    """After migration, the wrong password must still be rejected."""
    async with websockets.connect(SERVER_URI, ssl=ssl_ctx()) as ws:
        auth = await send_recv(ws, {
            "type": "auth",
            "data": {"username": "demo", "password": "definitely_wrong"},
        })
        assert auth["type"] == "error", \
            f"BUG: Argon2 verify accepted wrong password: {auth}"
        assert auth["data"]["code"] == 401, auth
        print("    ✓ Argon2 verify rejects wrong password")


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

TESTS = [
    ("T1  wrong password rejected (no bypass)", t1_wrong_password_rejected),
    ("T2  new users use Argon2id",              t2_new_users_use_argon2),
    ("T3  legacy SHA-256 migrates on login",    t3_legacy_hash_migrates_on_login),
    ("T4  post-migration login works",          t4_argon2_verify_still_works),
    ("T5  Argon2 rejects wrong password",       t5_argon2_verify_rejects_wrong_password),
]


async def main():
    print("=" * 64)
    print(" Password Hashing Test Suite")
    print(f" Server: {SERVER_URI}")
    print(f" DB:     {DB_PATH}")
    print("=" * 64)

    # Preflight: server reachable?
    try:
        async with websockets.connect(SERVER_URI, ssl=ssl_ctx(), open_timeout=3.0):
            pass
    except Exception as e:
        print(f"\n❌ Cannot connect to server: {e}")
        sys.exit(1)

    # Preflight: DB reachable?
    try:
        db_query("SELECT 1")
    except Exception as e:
        print(f"\n❌ Cannot read DB at {DB_PATH}: {e}")
        sys.exit(1)

    passed = failed = 0
    failures = []

    for name, fn in TESTS:
        print(f"\n▶ {name}")
        try:
            await fn()
            passed += 1
        except AssertionError as e:
            failed += 1
            failures.append((name, str(e)))
            print(f"    ❌ {e}")
        except Exception as e:
            failed += 1
            failures.append((name, f"{type(e).__name__}: {e}"))
            print(f"    ❌ {type(e).__name__}: {e}")

    print("\n" + "=" * 64)
    print(f"SUMMARY: {passed}/{len(TESTS)} passed, {failed} failed")
    for name, reason in failures:
        print(f"  • {name}: {reason}")
    print("=" * 64)
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    asyncio.run(main())