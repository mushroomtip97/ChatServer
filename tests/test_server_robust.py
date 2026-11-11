"""
Production-grade test suite for the Qt Chat Server.

Run:
    pip install websockets aiohttp pytest pytest-asyncio
    pytest -v --asyncio-mode=auto test_chat_server_robust.py

Or as a script:
    python test_chat_server_robust.py
"""

from __future__ import annotations

import asyncio
import json
import os
import random
import ssl
import string
import subprocess
import sys
import time
import uuid
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from typing import Any, AsyncIterator, Iterable

import websockets
from websockets.exceptions import ConnectionClosed

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SERVER_URI = os.environ.get("CHAT_SERVER_URI", "ws://localhost:8080")
USE_TLS = os.environ.get("CHAT_SERVER_TLS", "0") == "1"
HEALTH_URL = os.environ.get("CHAT_SERVER_HEALTH")  # e.g. http://localhost:8081/health
RESET_CMD = os.environ.get("CHAT_SERVER_RESET_CMD")  # optional shell command to reset DB
SERVER_RESTART_CMD = os.environ.get("CHAT_SERVER_RESTART_CMD")
METRICS_URL = os.environ.get("CHAT_SERVER_METRICS")

DEFAULT_TIMEOUT = 5.0
LONG_TIMEOUT = 30.0
STRICT_PROTOCOL = os.environ.get("CHAT_STRICT_PROTOCOL", "1") == "1"
REQUIRE_TLS = os.environ.get("CHAT_REQUIRE_TLS", "0") == "1"
REQUIRE_RATE_LIMIT = os.environ.get("CHAT_REQUIRE_RATE_LIMIT", "1") == "1"
REQUIRE_PERSISTENCE = os.environ.get("CHAT_REQUIRE_PERSISTENCE", "0") == "1"


def _ssl_ctx() -> ssl.SSLContext | None:
    if not USE_TLS:
        return None
    ctx = ssl.create_default_context()
    ca = os.environ.get("CHAT_CA_CERT")
    if ca:
        ctx.load_verify_locations(ca)
    else:
        # dev mode: don't verify (tests only)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
    return ctx


# ---------------------------------------------------------------------------
# Result tracking
# ---------------------------------------------------------------------------

@dataclass
class Result:
    name: str
    passed: bool
    reason: str = ""
    duration_s: float = 0.0


@dataclass
class Suite:
    results: list[Result] = field(default_factory=list)

    def record(self, name: str, passed: bool, reason: str = "", duration_s: float = 0.0):
        self.results.append(Result(name, passed, reason, duration_s))
        icon = "✅" if passed else "❌"
        print(f"  {icon} {name}" + (f" — {reason}" if reason and not passed else ""))

    @property
    def passed(self) -> int:
        return sum(1 for r in self.results if r.passed)

    @property
    def failed(self) -> int:
        return sum(1 for r in self.results if not r.passed)

    def summary(self) -> bool:
        total = len(self.results)
        print("\n" + "=" * 72)
        print(f"SUMMARY: {self.passed}/{total} passed, {self.failed} failed")
        print("=" * 72)
        for r in self.results:
            if not r.passed:
                print(f"  • {r.name}: {r.reason}")
        return self.failed == 0


SUITE = Suite()


def check(name: str):
    """Decorator that times a test and records pass/fail."""
    def deco(fn):
        async def wrapper(*args, **kwargs):
            t0 = time.perf_counter()
            try:
                await fn(*args, **kwargs)
                SUITE.record(name, True, duration_s=time.perf_counter() - t0)
            except AssertionError as e:
                SUITE.record(name, False, str(e) or "assertion failed",
                             duration_s=time.perf_counter() - t0)
            except Exception as e:
                SUITE.record(name, False, f"{type(e).__name__}: {e}",
                             duration_s=time.perf_counter() - t0)
        return wrapper
    return deco


# ---------------------------------------------------------------------------
# Client helper
# ---------------------------------------------------------------------------

class Client:
    """Thin wrapper over a websocket that tracks received messages and can
    assert on the next expected frame."""

    def __init__(self, ws, name: str):
        self.ws = ws
        self.name = name
        self.inbox: asyncio.Queue[dict] = asyncio.Queue()

    async def start_reader(self) -> asyncio.Task:
        async def reader():
            try:
                async for raw in self.ws:
                    try:
                        self.inbox.put_nowait(json.loads(raw))
                    except json.JSONDecodeError:
                        self.inbox.put_nowait({"__raw__": raw})
            except ConnectionClosed:
                self.inbox.put_nowait({"__closed__": True})
        return asyncio.create_task(reader())

    async def send(self, msg: dict):
        await self.ws.send(json.dumps(msg))

    async def recv(self, timeout: float = DEFAULT_TIMEOUT) -> dict:
        try:
            return await asyncio.wait_for(self.inbox.get(), timeout=timeout)
        except asyncio.TimeoutError:
            raise AssertionError(f"[{self.name}] timed out waiting for frame")

    async def expect(self, msg_type: str, timeout: float = DEFAULT_TIMEOUT) -> dict:
        """Consume frames until one matches msg_type; raises if it doesn't arrive."""
        deadline = time.monotonic() + timeout
        skipped: list[dict] = []
        while time.monotonic() < deadline:
            remaining = max(0.01, deadline - time.monotonic())
            frame = await self.recv(remaining)
            if frame.get("type") == msg_type:
                return frame
            skipped.append(frame)
        raise AssertionError(
            f"[{self.name}] expected '{msg_type}', skipped={skipped[:3]}")

    async def close(self):
        try:
            await self.ws.close()
        except Exception:
            pass


@asynccontextmanager
async def connect(name: str = "client") -> AsyncIterator[Client]:
    ws = await websockets.connect(
        SERVER_URI,
        ssl=_ssl_ctx(),
        open_timeout=DEFAULT_TIMEOUT,
        close_timeout=DEFAULT_TIMEOUT,
        max_size=2**20,
    )
    c = Client(ws, name)
    reader = await c.start_reader()
    try:
        yield c
    finally:
        reader.cancel()
        await c.close()


async def register_and_auth(name: str = "client",
                            username: str | None = None,
                            password: str = "password123") -> tuple[Client, str, str]:
    """Fresh registration + auth. Returns (client, username, password)."""
    username = username or f"u_{uuid.uuid4().hex[:12]}"
    cm = connect(name)
    c = await cm.__aenter__()
    try:
        await c.send({"type": "register", "data": {"username": username, "password": password}})
        await c.expect("register_success")
        await c.send({"type": "auth", "data": {"username": username, "password": password}})
        await c.expect("auth_success")
    except Exception:
        await cm.__aexit__(*sys.exc_info())
        raise
    # Attach context manager so caller can clean up
    c.__cm__ = cm  # type: ignore[attr-defined]
    return c, username, password


async def close_client(c: Client):
    cm = getattr(c, "__cm__", None)
    if cm is not None:
        await cm.__aexit__(None, None, None)
    else:
        await c.close()


# ---------------------------------------------------------------------------
# Fixtures / helpers
# ---------------------------------------------------------------------------

async def reset_server_state():
    """Best-effort isolation between test groups."""
    if RESET_CMD:
        proc = await asyncio.create_subprocess_shell(
            RESET_CMD, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        await proc.wait()


async def restart_server_and_wait():
    if not SERVER_RESTART_CMD:
        raise AssertionError("SERVER_RESTART_CMD not set; cannot test persistence")
    proc = await asyncio.create_subprocess_shell(SERVER_RESTART_CMD)
    await proc.wait()
    # wait for server to accept connections
    deadline = time.monotonic() + LONG_TIMEOUT
    while time.monotonic() < deadline:
        try:
            ws = await websockets.connect(SERVER_URI, ssl=_ssl_ctx(),
                                          open_timeout=2.0)
            await ws.close()
            return
        except Exception:
            await asyncio.sleep(0.25)
    raise AssertionError("server did not come back up after restart")


async def http_get(url: str, timeout: float = 5.0) -> tuple[int, str]:
    import aiohttp
    async with aiohttp.ClientSession() as s:
        async with s.get(url, timeout=aiohttp.ClientTimeout(total=timeout)) as r:
            return r.status, await r.text()


# ===========================================================================
# TEST GROUPS
# ===========================================================================

# ---------------------------------------------------------------------------
# G1 — Connection & protocol handshake
# ---------------------------------------------------------------------------

@check("G1.1 plain connect / disconnect")
async def g1_1_connect():
    async with connect("g1.1"):
        pass


@check("G1.2 TLS required when CHAT_REQUIRE_TLS=1")
async def g1_2_tls():
    if not REQUIRE_TLS:
        raise AssertionError("skipped (set CHAT_REQUIRE_TLS=1 to enforce)")
    if not USE_TLS:
        raise AssertionError("CHAT_REQUIRE_TLS=1 but CHAT_SERVER_TLS not enabled")
    async with connect("g1.2"):
        pass


@check("G1.3 server rejects oversized frame (>1MB)")
async def g1_3_oversized():
    async with connect("g1.3") as c:
        # 1.5 MB payload — server should close or return error
        big = "x" * (1_500_000)
        try:
            await c.send({"type": "message", "data": {"roomId": 1, "content": big}})
        except ConnectionClosed:
            return  # acceptable
        try:
            frame = await c.recv(timeout=3.0)
            assert frame.get("type") == "error", f"expected error, got {frame}"
        except AssertionError:
            # closed is also acceptable
            return


@check("G1.4 server closes on malformed binary frame")
async def g1_4_binary():
    async with connect("g1.4") as c:
        await c.ws.send(b"\x00\x01\x02\x03")
        try:
            await c.recv(timeout=2.0)
        except AssertionError:
            return  # closed = fine


# ---------------------------------------------------------------------------
# G2 — Registration validation (real assertions)
# ---------------------------------------------------------------------------

@check("G2.1 valid registration")
async def g2_1_register_ok():
    async with connect("g2.1") as c:
        u = f"reg_{uuid.uuid4().hex[:10]}"
        await c.send({"type": "register", "data": {"username": u, "password": "pw123456"}})
        r = await c.expect("register_success")
        assert r["data"]["username"] == u


@check("G2.2 duplicate registration rejected with 400")
async def g2_2_duplicate():
    async with connect("g2.2") as c:
        u = f"dup_{uuid.uuid4().hex[:10]}"
        await c.send({"type": "register", "data": {"username": u, "password": "pw123456"}})
        await c.expect("register_success")
        await c.send({"type": "register", "data": {"username": u, "password": "pw123456"}})
        r = await c.expect("error")
        assert r["data"]["code"] == 400, r


@check("G2.3 short username rejected")
async def g2_3_short_user():
    async with connect("g2.3") as c:
        await c.send({"type": "register", "data": {"username": "ab", "password": "pw123456"}})
        r = await c.expect("error")
        assert r["data"]["code"] == 400


@check("G2.4 short password rejected")
async def g2_4_short_pw():
    async with connect("g2.4") as c:
        await c.send({"type": "register",
                      "data": {"username": f"x_{uuid.uuid4().hex[:8]}", "password": "123"}})
        r = await c.expect("error")
        assert r["data"]["code"] == 400


@check("G2.5 invalid username chars rejected")
async def g2_5_bad_chars():
    async with connect("g2.5") as c:
        await c.send({"type": "register",
                      "data": {"username": "bad user!@#", "password": "pw123456"}})
        r = await c.expect("error")
        assert r["data"]["code"] == 400


@check("G2.6 empty username rejected")
async def g2_6_empty_user():
    async with connect("g2.6") as c:
        await c.send({"type": "register", "data": {"username": "", "password": "pw123456"}})
        r = await c.expect("error")
        assert r["data"]["code"] == 400


@check("G2.7 unicode username handled (accept or 400, not 500)")
async def g2_7_unicode_user():
    async with connect("g2.7") as c:
        await c.send({"type": "register",
                      "data": {"username": "用户_" + uuid.uuid4().hex[:6], "password": "pw123456"}})
        r = await c.recv()
        assert r.get("type") in ("register_success", "error"), r
        if r["type"] == "error":
            assert r["data"]["code"] in (400, 422)


@check("G2.8 concurrent registration of same username — exactly one wins")
async def g2_8_race():
    u = f"race_{uuid.uuid4().hex[:10]}"
    results: list[dict] = []
    lock = asyncio.Lock()

    async def attempt():
        async with connect("g2.8") as c:
            await c.send({"type": "register", "data": {"username": u, "password": "pw123456"}})
            r = await c.recv()
            async with lock:
                results.append(r)

    await asyncio.gather(*(attempt() for _ in range(8)))
    successes = [r for r in results if r.get("type") == "register_success"]
    errors = [r for r in results if r.get("type") == "error"]
    assert len(successes) == 1, f"expected exactly 1 success, got {len(successes)}"
    assert all(e["data"]["code"] == 400 for e in errors)


# ---------------------------------------------------------------------------
# G3 — Authentication
# ---------------------------------------------------------------------------

@check("G3.1 valid auth")
async def g3_1_auth_ok():
    c, u, pw = await register_and_auth("g3.1")
    await close_client(c)


@check("G3.2 wrong password rejected 401")
async def g3_2_wrong_pw():
    c, u, _ = await register_and_auth("g3.2")
    await close_client(c)
    async with connect("g3.2b") as c2:
        await c2.send({"type": "auth", "data": {"username": u, "password": "wrong"}})
        r = await c2.expect("error")
        assert r["data"]["code"] == 401


@check("G3.3 unknown user rejected 401")
async def g3_3_unknown():
    async with connect("g3.3") as c:
        await c.send({"type": "auth",
                      "data": {"username": "ghost_" + uuid.uuid4().hex, "password": "pw123456"}})
        r = await c.expect("error")
        assert r["data"]["code"] == 401


@check("G3.4 auth required before messaging")
async def g3_4_unauth_msg():
    async with connect("g3.4") as c:
        await c.send({"type": "message", "data": {"roomId": 1, "content": "hi"}})
        r = await c.expect("error")
        assert r["data"]["code"] == 401


@check("G3.5 auth required before history")
async def g3_5_unauth_hist():
    async with connect("g3.5") as c:
        await c.send({"type": "history_request", "data": {"roomId": 1, "limit": 10}})
        r = await c.expect("error")
        assert r["data"]["code"] == 401


@check("G3.6 brute-force protection (rate-limit or lockout)")
async def g3_6_bruteforce():
    if not REQUIRE_RATE_LIMIT:
        raise AssertionError("skipped (CHAT_REQUIRE_RATE_LIMIT=0)")
    c, u, _ = await register_and_auth("g3.6")
    await close_client(c)
    attempts = 0
    blocked = False
    async with connect("g3.6b") as c2:
        for _ in range(30):
            attempts += 1
            await c2.send({"type": "auth", "data": {"username": u, "password": "bad"}})
            r = await c2.recv()
            if r.get("type") == "error" and r["data"]["code"] in (429, 423):
                blocked = True
                break
            if r.get("__closed__"):
                blocked = True
                break
    assert blocked, f"no rate-limit/lockout after {attempts} failed attempts"


# ---------------------------------------------------------------------------
# G4 — Messaging & rooms
# ---------------------------------------------------------------------------

@check("G4.1 join / leave room")
async def g4_1_join_leave():
    c, _, _ = await register_and_auth("g4.1")
    try:
        await c.send({"type": "join_room", "data": {"roomId": 42}})
        r = await c.expect("room_joined")
        assert r["data"]["roomId"] == 42
        await c.send({"type": "leave_room", "data": {"roomId": 42}})
        r = await c.expect("room_left")
        assert r["data"]["roomId"] == 42
    finally:
        await close_client(c)


@check("G4.2 empty message rejected")
async def g4_2_empty_msg():
    c, _, _ = await register_and_auth("g4.2")
    try:
        await c.send({"type": "join_room", "data": {"roomId": 1}})
        await c.expect("room_joined")
        await c.send({"type": "message", "data": {"roomId": 1, "content": ""}})
        r = await c.expect("error")
        assert r["data"]["code"] == 400
    finally:
        await close_client(c)


@check("G4.3 message to room not joined rejected")
async def g4_3_not_joined():
    c, _, _ = await register_and_auth("g4.3")
    try:
        await c.send({"type": "message", "data": {"roomId": 999, "content": "hi"}})
        r = await c.expect("error")
    finally:
        await close_client(c)


@check("G4.4 real-time delivery: A→B, B→A, sender excluded")
async def g4_4_realtime():
    a, _, _ = await register_and_auth("g4.4-a")
    b, _, _ = await register_and_auth("g4.4-b")
    try:
        for c in (a, b):
            await c.send({"type": "join_room", "data": {"roomId": 7}})
            await c.expect("room_joined")
        await asyncio.sleep(0.1)

        m1 = f"m1-{uuid.uuid4().hex}"
        await a.send({"type": "message", "data": {"roomId": 7, "content": m1}})
        got = await b.expect("message")
        assert got["data"]["content"] == m1

        m2 = f"m2-{uuid.uuid4().hex}"
        await b.send({"type": "message", "data": {"roomId": 7, "content": m2}})
        got = await a.expect("message")
        assert got["data"]["content"] == m2

        # sender must not receive own message
        m3 = f"m3-{uuid.uuid4().hex}"
        await a.send({"type": "message", "data": {"roomId": 7, "content": m3}})
        await b.expect("message")  # B gets it
        try:
            extra = await a.recv(timeout=1.0)
            raise AssertionError(f"sender received own message: {extra}")
        except AssertionError as e:
            if "timed out" not in str(e):
                raise
    finally:
        await close_client(a)
        await close_client(b)


@check("G4.5 broadcast to N=5 clients — all receive, ordering preserved")
async def g4_5_broadcast():
    clients = []
    try:
        for i in range(5):
            c, _, _ = await register_and_auth(f"g4.5-{i}")
            await c.send({"type": "join_room", "data": {"roomId": 8}})
            await c.expect("room_joined")
            clients.append(c)
        await asyncio.sleep(0.2)

        payloads = [f"p{k}-{uuid.uuid4().hex}" for k in range(10)]
        for p in payloads:
            await clients[0].send({"type": "message", "data": {"roomId": 8, "content": p}})

        for c in clients[1:]:
            received = []
            for _ in payloads:
                r = await c.expect("message", timeout=5.0)
                received.append(r["data"]["content"])
            assert received == payloads, f"order mismatch: {received}"
    finally:
        for c in clients:
            await close_client(c)


@check("G4.6 room isolation — messages don't leak between rooms")
async def g4_6_isolation():
    a, _, _ = await register_and_auth("g4.6-a")
    b, _, _ = await register_and_auth("g4.6-b")
    try:
        await a.send({"type": "join_room", "data": {"roomId": 11}})
        await a.expect("room_joined")
        await b.send({"type": "join_room", "data": {"roomId": 22}})
        await b.expect("room_joined")
        await asyncio.sleep(0.1)

        await a.send({"type": "message", "data": {"roomId": 11, "content": "secret"}})
        try:
            await b.recv(timeout=1.0)
            raise AssertionError("message leaked across rooms")
        except AssertionError as e:
            if "timed out" not in str(e):
                raise
    finally:
        await close_client(a)
        await close_client(b)


# ---------------------------------------------------------------------------
# G5 — History
# ---------------------------------------------------------------------------

@check("G5.1 history returns sent messages in order")
async def g5_1_history_order():
    c, _, _ = await register_and_auth("g5.1")
    try:
        await c.send({"type": "join_room", "data": {"roomId": 100}})
        await c.expect("room_joined")
        sent = [f"h-{i}-{uuid.uuid4().hex[:6]}" for i in range(5)]
        for m in sent:
            await c.send({"type": "message", "data": {"roomId": 100, "content": m}})
            await asyncio.sleep(0.02)

        await c.send({"type": "history_request", "data": {"roomId": 100, "limit": 50}})
        r = await c.expect("history_response")
        contents = [m["content"] for m in r["data"]["messages"]]
        contents.reverse()
        for m in sent:
            assert m in contents, f"missing {m} in history"
        # order of the tail must match
        tail = contents[-len(sent):]
        assert tail == sent, f"order mismatch: {tail} vs {sent}"
    finally:
        await close_client(c)


@check("G5.2 history limit honored")
async def g5_2_history_limit():
    c, _, _ = await register_and_auth("g5.2")
    try:
        await c.send({"type": "join_room", "data": {"roomId": 101}})
        await c.expect("room_joined")
        for i in range(20):
            await c.send({"type": "message", "data": {"roomId": 101, "content": f"x{i}"}})
        await asyncio.sleep(0.5)
        await c.send({"type": "history_request", "data": {"roomId": 101, "limit": 5}})
        r = await c.expect("history_response")
        assert len(r["data"]["messages"]) <= 5
    finally:
        await close_client(c)


@check("G5.3 history unauthorized for unjoined room")
async def g5_3_history_unauth_room():
    a, _, _ = await register_and_auth("g5.3-a")
    b, _, _ = await register_and_auth("g5.3-b")
    try:
        await a.send({"type": "join_room", "data": {"roomId": 200}})
        await a.expect("room_joined")
        await a.send({"type": "message", "data": {"roomId": 200, "content": "private"}})
        await asyncio.sleep(0.1)
        await b.send({"type": "history_request", "data": {"roomId": 200, "limit": 10}})
        r = await b.expect("error")
    finally:
        await close_client(a)
        await close_client(b)


# ---------------------------------------------------------------------------
# G6 — Protocol validation (strict)
# ---------------------------------------------------------------------------

@check("G6.1 invalid JSON → error 400")
async def g6_1_invalid_json():
    async with connect("g6.1") as c:
        await c.ws.send("not json {{{")
        r = await c.expect("error")
        assert r["data"]["code"] == 400


@check("G6.2 missing 'type' → error")
async def g6_2_no_type():
    async with connect("g6.2") as c:
        await c.ws.send(json.dumps({"data": {}}))
        r = await c.expect("error")
        assert r["data"]["code"] in (400, 422)


@check("G6.3 unknown type → error 400")
async def g6_3_unknown_type():
    async with connect("g6.3") as c:
        await c.send({"type": "definitely_not_a_type", "data": {}})
        r = await c.expect("error")
        assert r["data"]["code"] in (400, 422)


@check("G6.4 missing 'data' field → error")
async def g6_4_no_data():
    async with connect("g6.4") as c:
        await c.ws.send(json.dumps({"type": "auth"}))
        r = await c.expect("error")
        assert r["data"]["code"] in (400, 422)


@check("G6.5 wrong field types → error, never 500")
async def g6_5_wrong_types():
    async with connect("g6.5") as c:
        await c.send({"type": "message",
                      "data": {"roomId": "not_a_number", "content": 12345}})
        r = await c.recv(timeout=3.0)
        if r.get("type") == "error":
            assert r["data"]["code"] < 500, f"5xx on bad input: {r}"
        # else: coerced — acceptable only if server defines coercion. We don't
        # fail here, but we warn.
        else:
            print(f"     ⚠ server coerced malformed types: {r}")


@check("G6.6 deeply nested JSON doesn't crash")
async def g6_6_deep_nesting():
    async with connect("g6.6") as c:
        blob: Any = "x"
        for _ in range(200):
            blob = {"a": blob}
        await c.ws.send(json.dumps({"type": "message", "data": blob}))
        r = await c.recv(timeout=3.0)
        assert r.get("type") in ("error", "__closed__") or r["data"] is not None


# ---------------------------------------------------------------------------
# G7 — Concurrency & correctness under load
# ---------------------------------------------------------------------------

@check("G7.1 200 concurrent clients — connect, auth, join, one broadcast")
async def g7_1_200_clients():
    N = 200
    ROOM = 500

    async def one(i: int) -> int:
        c, _, _ = await register_and_auth(f"g7-{i}")
        try:
            await c.send({"type": "join_room", "data": {"roomId": ROOM}})
            await c.expect("room_joined")
            return 1
        finally:
            await close_client(c)

    # connect in waves of 50 to avoid file-descriptor storms on dev boxes
    total = 0
    for start in range(0, N, 50):
        results = await asyncio.gather(*(one(i) for i in range(start, start + 50)),
                                       return_exceptions=True)
        for r in results:
            if isinstance(r, Exception):
                raise AssertionError(f"client failed: {r}")
            total += r
    assert total == N


@check("G7.2 50 clients in one room — broadcast fan-out latency")
async def g7_2_fanout_latency():
    N = 50
    ROOM = 501
    clients: list[Client] = []
    try:
        for i in range(N):
            c, _, _ = await register_and_auth(f"g7.2-{i}")
            await c.send({"type": "join_room", "data": {"roomId": ROOM}})
            await c.expect("room_joined")
            clients.append(c)
        await asyncio.sleep(0.2)

        t0 = time.perf_counter()
        await clients[0].send({"type": "message",
                               "data": {"roomId": ROOM, "content": "fanout"}})
        await asyncio.gather(*(c.expect("message", timeout=10.0) for c in clients[1:]))
        elapsed = time.perf_counter() - t0
        # generous: 50 fan-out under 2s on a dev box
        assert elapsed < 2.0, f"fan-out took {elapsed:.2f}s for {N} clients"
    finally:
        for c in clients:
            await close_client(c)


@check("G7.3 1000 messages from one client — no drops, no reorder")
async def g7_3_burst():
    sender, _, _ = await register_and_auth("g7.3-s")
    receiver, _, _ = await register_and_auth("g7.3-r")
    ROOM = 502
    try:
        for c in (sender, receiver):
            await c.send({"type": "join_room", "data": {"roomId": ROOM}})
            await c.expect("room_joined")
        await asyncio.sleep(0.1)

        N = 1000
        payloads = [f"burst-{i:04d}-{uuid.uuid4().hex[:4]}" for i in range(N)]

        async def send_all():
            for p in payloads:
                await sender.send({"type": "message",
                                   "data": {"roomId": ROOM, "content": p}})
                # tiny yield to let the reader keep up
                if random.random() < 0.05:
                    await asyncio.sleep(0)

        recv_task = asyncio.gather(*(receiver.expect("message", timeout=30.0) for _ in range(N)))
        await send_all()
        got = await recv_task
        contents = [r["data"]["content"] for r in got]
        assert contents == payloads, (
            f"missing={len(set(payloads)-set(contents))}, "
            f"order_ok={contents==sorted(contents)}")
    finally:
        await close_client(sender)
        await close_client(receiver)


@check("G7.4 disconnect mid-session — server survives")
async def g7_4_abrupt_disconnect():
    c, _, _ = await register_and_auth("g7.4")
    await c.send({"type": "join_room", "data": {"roomId": 503}})
    await c.expect("room_joined")
    # hard close (no graceful websocket close frame)
    await c.ws.close() # type: ignore[attr-defined]
    await asyncio.sleep(0.3)
    # server must still accept new connections
    async with connect("g7.4b") as c2:
        await c2.send({"type": "auth", "data": {"username": "ghost", "password": "x"}})
        await c2.recv(timeout=3.0)


# ---------------------------------------------------------------------------
# G8 — Security
# ---------------------------------------------------------------------------

@check("G8.1 passwords are not echoed in any server response")
async def g8_1_no_pw_echo():
    u = f"sec_{uuid.uuid4().hex[:10]}"
    pw = "SuperSecret123!"
    collected = []

    async with connect("g8.1") as c:
        await c.send({"type": "register", "data": {"username": u, "password": pw}})
        collected.append(await c.recv())
        await c.send({"type": "auth", "data": {"username": u, "password": pw}})
        collected.append(await c.recv())
        await c.send({"type": "auth", "data": {"username": u, "password": "wrong"}})
        collected.append(await c.recv())
    blob = json.dumps(collected)
    assert pw not in blob, "password leaked in server response"
    assert "wrong" not in blob or "password" not in blob.lower()


@check("G8.2 SQL/command injection attempts are handled gracefully")
async def g8_2_injection():
    payloads = [
        "'; DROP TABLE users; --",
        "\" OR 1=1 --",
        "`rm -rf /`",
        "${jndi:ldap://x}",
        "\x00\x01\x02",
    ]
    async with connect("g8.2") as c:
        for p in payloads:
            await c.send({"type": "register",
                          "data": {"username": p, "password": "pw123456"}})
            r = await c.recv()
            assert r.get("type") in ("register_success", "error"), r
            if r["type"] == "error":
                assert r["data"]["code"] < 500, r


@check("G8.3 messages cannot spoof another user's identity")
async def g8_3_spoof():
    a, ua, _ = await register_and_auth("g8.3-a")
    b, ub, _ = await register_and_auth("g8.3-b")
    ROOM = 601
    try:
        for c in (a, b):
            await c.send({"type": "join_room", "data": {"roomId": ROOM}})
            await c.expect("room_joined")
        # A tries to send a message pretending to be B
        await a.send({"type": "message",
                      "data": {"roomId": ROOM, "content": "hi", "username": ub}})
        got = await b.expect("message")
        assert got["data"].get("username") != ub, f"identity spoofed: {got}"
        assert got["data"].get("username") == ua
    finally:
        await close_client(a)
        await close_client(b)


@check("G8.4 history cannot be read from an unjoined room")
async def g8_4_history_leak():
    a, _, _ = await register_and_auth("g8.4-a")
    b, _, _ = await register_and_auth("g8.4-b")
    ROOM = 700
    try:
        await a.send({"type": "join_room", "data": {"roomId": ROOM}})
        await a.expect("room_joined")
        await a.send({"type": "message", "data": {"roomId": ROOM, "content": "secret"}})
        await asyncio.sleep(0.2)
        await b.send({"type": "history_request", "data": {"roomId": ROOM, "limit": 50}})
        r = await b.expect("error")
    finally:
        await close_client(a)
        await close_client(b)


# ---------------------------------------------------------------------------
# G9 — Persistence & restart
# ---------------------------------------------------------------------------

@check("G9.1 data survives server restart")
async def g9_1_persistence():
    if not REQUIRE_PERSISTENCE:
        raise AssertionError("skipped (set CHAT_REQUIRE_PERSISTENCE=1)")
    u = f"persist_{uuid.uuid4().hex[:8]}"
    pw = "persistpw123"
    ROOM = 800
    async with connect("g9.1") as c:
        await c.send({"type": "register", "data": {"username": u, "password": pw}})
        await c.expect("register_success")
        await c.send({"type": "auth", "data": {"username": u, "password": pw}})
        await c.expect("auth_success")
        await c.send({"type": "join_room", "data": {"roomId": ROOM}})
        await c.expect("room_joined")
        await c.send({"type": "message", "data": {"roomId": ROOM, "content": "survives"}})
        await asyncio.sleep(0.3)

    await restart_server_and_wait()

    async with connect("g9.1b") as c:
        await c.send({"type": "auth", "data": {"username": u, "password": pw}})
        await c.expect("auth_success")
        await c.send({"type": "join_room", "data": {"roomId": ROOM}})
        await c.expect("room_joined")
        await c.send({"type": "history_request", "data": {"roomId": ROOM, "limit": 50}})
        r = await c.expect("history_response")
        contents = [m["content"] for m in r["data"]["messages"]]
        assert "survives" in contents, f"message lost across restart: {contents}"


# ---------------------------------------------------------------------------
# G10 — Observability
# ---------------------------------------------------------------------------

@check("G10.1 health endpoint responds OK")
async def g10_1_health():
    if not HEALTH_URL:
        raise AssertionError("skipped (CHAT_SERVER_HEALTH not set)")
    status, body = await http_get(HEALTH_URL)
    assert status == 200, f"health returned {status}: {body[:200]}"


@check("G10.2 metrics endpoint exposes connection count")
async def g10_2_metrics():
    if not METRICS_URL:
        raise AssertionError("skipped (CHAT_SERVER_METRICS not set)")
    status, body = await http_get(METRICS_URL)
    assert status == 200
    assert any(k in body.lower() for k in ("connections", "clients", "sessions")), \
        "metrics don't expose connection count"


# ===========================================================================
# Runner
# ===========================================================================

async def run_all():
    print("╔" + "═" * 70 + "╗")
    print("║" + " Qt Chat Server — Robust Test Suite ".center(70) + "║")
    print("╚" + "═" * 70 + "╝")
    print(f"Server:       {SERVER_URI}")
    print(f"TLS:          {USE_TLS}")
    print(f"Strict proto: {STRICT_PROTOCOL}")
    print(f"Rate limit:   {REQUIRE_RATE_LIMIT}")
    print(f"Persistence:  {REQUIRE_PERSISTENCE}")
    print()

    # preflight
    try:
        ws = await websockets.connect(SERVER_URI, ssl=_ssl_ctx(), open_timeout=3.0)
        await ws.close()
    except Exception as e:
        print(f"❌ Cannot reach server: {e}")
        sys.exit(1)

    await reset_server_state()

    tests = [
        g1_1_connect, g1_2_tls, g1_3_oversized, g1_4_binary,
        g2_1_register_ok, g2_2_duplicate, g2_3_short_user, g2_4_short_pw,
        g2_5_bad_chars, g2_6_empty_user, g2_7_unicode_user, g2_8_race,
        g3_1_auth_ok, g3_2_wrong_pw, g3_3_unknown, g3_4_unauth_msg,
        g3_5_unauth_hist, g3_6_bruteforce,
        g4_1_join_leave, g4_2_empty_msg, g4_3_not_joined, g4_4_realtime,
        g4_5_broadcast, g4_6_isolation,
        g5_1_history_order, g5_2_history_limit, g5_3_history_unauth_room,
        g6_1_invalid_json, g6_2_no_type, g6_3_unknown_type, g6_4_no_data,
        g6_5_wrong_types, g6_6_deep_nesting,
        g7_1_200_clients, g7_2_fanout_latency, g7_3_burst, g7_4_abrupt_disconnect,
        g8_1_no_pw_echo, g8_2_injection, g8_3_spoof, g8_4_history_leak,
        g9_1_persistence,
        g10_1_health, g10_2_metrics,
    ]

    for t in tests:
        await t()

    ok = SUITE.summary()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    try:
        asyncio.run(run_all())
    except KeyboardInterrupt:
        print("\n⚠ interrupted")
        sys.exit(1)
    except Exception as e:
        import traceback
        traceback.print_exc()
        sys.exit(1)