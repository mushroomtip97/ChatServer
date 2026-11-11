"""
Presence broadcast test suite.

Assumes the server broadcasts presence_update to every authenticated client
(Model 1). If your server requires an explicit presence_subscribe, the test
sends it once after auth and the flow is otherwise identical.
"""

import asyncio
import json
import sys
import time
import uuid
import websockets

from tls_helper import SERVER_URI, connect_kwargs
DEFAULT_TIMEOUT = 5.0
REQUIRES_SUBSCRIBE = True   # set False if your server broadcasts unconditionally


class Client:
    def __init__(self, ws, name):
        self.ws = ws
        self.name = name

    async def send(self, obj):
        await self.ws.send(json.dumps(obj))

    async def recv(self, timeout=DEFAULT_TIMEOUT):
        raw = await asyncio.wait_for(self.ws.recv(), timeout=timeout)
        return json.loads(raw)

    async def expect(self, msg_type, timeout=DEFAULT_TIMEOUT):
        """Read frames until one matches msg_type (skips others)."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            remaining = max(0.01, deadline - time.monotonic())
            frame = await self.recv(timeout=remaining)
            if frame.get("type") == msg_type:
                return frame
        raise AssertionError(f"[{self.name}] never saw '{msg_type}'")

    async def drain(self, settle=0.2):
        """Consume any pending frames within `settle` seconds."""
        while True:
            try:
                await self.recv(timeout=settle)
            except asyncio.TimeoutError:
                return

    async def close(self):
        try:
            await self.ws.close()
        except Exception:
            pass


async def connect_and_auth(name):
    username = f"pr_{uuid.uuid4().hex[:10]}"
    password = "testpass123"

    ws = await websockets.connect(SERVER_URI, open_timeout=DEFAULT_TIMEOUT, **connect_kwargs())
    c = Client(ws, name)

    await c.send({"type": "register",
                  "data": {"username": username, "password": password}})
    await c.expect("register_success")

    await c.send({"type": "auth",
                  "data": {"username": username, "password": password}})
    await c.expect("auth_success")

    if REQUIRES_SUBSCRIBE:
        await c.send({"type": "presence_subscribe", "data": {}})
        # Drain the initial presence_list (contents not asserted here)
        try:
            await c.expect("presence_list", timeout=2.0)
        except AssertionError:
            pass

    return c, username


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

async def t1_online_broadcast():
    """When Bob authenticates, already-connected Alice sees him come online."""
    alice, _ = await connect_and_auth("alice")
    await alice.drain(0.3)

    bob, b_name = await connect_and_auth("bob")
    try:
        update = await alice.expect("presence_update", timeout=3.0)
        data = update["data"]
        assert data["online"] is True, data
        assert data["username"] == b_name, data
        assert data["userId"], data   # should be a non-empty string
        print("    ✓ online update received")
    finally:
        await alice.close()
        await bob.close()


async def t2_offline_broadcast():
    """When Bob disconnects, Alice sees him go offline."""
    alice, _ = await connect_and_auth("alice")
    await alice.drain(0.3)

    bob, b_name = await connect_and_auth("bob")
    # Drain Alice's inbox of Bob's online update
    await alice.drain(0.5)

    # Bob disconnects
    await bob.close()

    try:
        update = await alice.expect("presence_update", timeout=3.0)
        data = update["data"]
        assert data["online"] is False, data
        assert data["username"] == b_name, data
        assert data["lastSeen"] > 0, data   # timestamp should be set
        print("    ✓ offline update received with lastSeen")
    finally:
        await alice.close()


async def t3_multi_session_keeps_online():
    """Bob with two sessions: closing one does NOT emit offline."""
    alice, _ = await connect_and_auth("alice")
    await alice.drain(0.3)

    bob1, b_name = await connect_and_auth("bob-1")
    bob2 = await connect_and_auth_session(b_name, "bob-2")
    # Drain Alice's online update(s)
    await alice.drain(0.5)

    # Close one session
    await bob1.close()

    # Alice should NOT see an offline update within a reasonable window
    try:
        frame = await asyncio.wait_for(alice.recv(timeout=1.5), timeout=1.5)
        if frame.get("type") == "presence_update" and frame["data"]["online"] is False:
            raise AssertionError(f"premature offline: {frame}")
        # other frame types are fine — keep waiting a moment
    except asyncio.TimeoutError:
        pass
    print("    ✓ no offline update while Bob still has a session")

    # Close second session — now offline should arrive
    await bob2.close()
    update = await alice.expect("presence_update", timeout=3.0)
    assert update["data"]["online"] is False, update
    assert update["data"]["username"] == b_name
    print("    ✓ offline update arrives after last session closes")

    await alice.close()


async def t4_presence_list_snapshot():
    """On subscribe, the server sends the current list of online users."""
    if not REQUIRES_SUBSCRIBE:
        print("    (skipped — server broadcasts unconditionally)")
        return

    # Put a couple of users online first
    u1, u1_name = await connect_and_auth("u1")
    u2, u2_name = await connect_and_auth("u2")

    # Now connect a fresh subscriber and check the snapshot
    probe, _ = await connect_and_auth("probe")
    try:
        # connect_and_auth already drained the list; ask again
        await probe.send({"type": "presence_subscribe", "data": {}})
        snap = await probe.expect("presence_list", timeout=3.0)
        users = snap["data"]["users"]
        names = [u["username"] for u in users]
        assert u1_name in names, f"{u1_name} missing from snapshot: {names}"
        assert u2_name in names, f"{u2_name} missing from snapshot: {names}"
        print(f"    ✓ snapshot contains {len(names)} users")
    finally:
        await probe.close()
        await u1.close()
        await u2.close()


async def t5_self_not_in_update():
    """Bob should not receive his own presence_update about himself.

    (Depends on server design. Broadcast-to-all usually excludes the subject.
    If yours doesn't, mark this test as informational rather than failing.)
    """
    bob, _ = await connect_and_auth("bob")
    await bob.drain(0.5)

    # Give Bob a moment to see if a self-update arrives
    try:
        frame = await asyncio.wait_for(bob.recv(timeout=1.0), timeout=1.0)
        if frame.get("type") == "presence_update" and frame["data"].get("username") == _:
            print("    ⚠ server sent self-update (not necessarily wrong)")
        else:
            print("    ✓ no self-update about self")
    except asyncio.TimeoutError:
        print("    ✓ no self-update about self")

    await bob.close()


async def connect_and_auth_session(username, name):
    """Reuse an existing user (multi-session test)."""
    ws = await websockets.connect(SERVER_URI, open_timeout=DEFAULT_TIMEOUT, **connect_kwargs())
    c = Client(ws, name)
    await c.send({"type": "auth",
                  "data": {"username": username, "password": "testpass123"}})
    await c.expect("auth_success")
    if REQUIRES_SUBSCRIBE:
        await c.send({"type": "presence_subscribe", "data": {}})
        try:
            await c.expect("presence_list", timeout=2.0)
        except AssertionError:
            pass
    return c


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

TESTS = [
    ("T1  online broadcast on connect",       t1_online_broadcast),
    ("T2  offline broadcast on disconnect",   t2_offline_broadcast),
    ("T3  multi-session keeps online",        t3_multi_session_keeps_online),
    ("T4  presence_list snapshot",            t4_presence_list_snapshot),
    ("T5  no self-update",                    t5_self_not_in_update),
]


async def main():
    print("=" * 64)
    print(" Presence Test Suite")
    print(f" Server: {SERVER_URI}")
    print(f" Subscribe required: {REQUIRES_SUBSCRIBE}")
    print("=" * 64)

    try:
        ws = await websockets.connect(SERVER_URI, open_timeout=DEFAULT_TIMEOUT, **connect_kwargs())
        await ws.close()
    except Exception as e:
        print(f"\n❌ Cannot connect: {e}")
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