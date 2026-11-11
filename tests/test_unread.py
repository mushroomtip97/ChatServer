"""

Unread counts + read receipts test suite.

Assumes:
- mark_read uses data.with + data.beforeMessageId
- unread_request has empty data
- Server sends unread_summary after mark_read (auto-refresh)
- Server sends read_receipt to the original sender's sessions
"""

import asyncio
import json
import sys
import time
import uuid
import websockets


from tls_helper import SERVER_URI, connect_kwargs
DEFAULT_TIMEOUT = 5.0


class Client:
    def __init__(self, ws, name):
        self.ws = ws
        self.name = name

    async def send(self, obj):
        await self.ws.send(json.dumps(obj))

    async def recv(self, timeout=DEFAULT_TIMEOUT):
        return json.loads(await asyncio.wait_for(self.ws.recv(), timeout=timeout))

    async def expect(self, msg_type, timeout=DEFAULT_TIMEOUT):
        deadline = time.monotonic() + timeout
        skipped = []
        while time.monotonic() < deadline:
            frame = await self.recv(timeout=max(0.01, deadline - time.monotonic()))
            if frame.get("type") == msg_type:
                return frame
            skipped.append(frame.get("type"))
        raise AssertionError(
            f"[{self.name}] never saw '{msg_type}'; skipped={skipped[:5]}")

    async def close(self):
        try:
            await self.ws.close()
        except Exception:
            pass


async def reg_auth(name):
    u = f"un_{uuid.uuid4().hex[:8]}"
    ws = await websockets.connect(SERVER_URI, open_timeout=DEFAULT_TIMEOUT, **connect_kwargs())
    c = Client(ws, name)
    await c.send({"type": "register", "data": {"username": u, "password": "testpass123"}})
    await c.expect("register_success")
    await c.send({"type": "auth", "data": {"username": u, "password": "testpass123"}})
    await c.expect("auth_success")
    return c, u


async def send_pm(sender, sender_name, recipient_name, content):
    await sender.send({"type": "private_message",
                       "data": {"to": recipient_name, "content": content}})
    return await sender.expect("private_message_sent")


async def get_unread(client):
    await client.send({"type": "unread_request", "data": {}})
    return await client.expect("unread_summary")


def find_conv(summary, peer_name):
    return next((c for c in summary["data"]["conversations"] if c["with"] == peer_name), None)


# ---------------------------------------------------------------------------

async def t1_summary_counts_received_messages():
    alice, a_name = await reg_auth("alice")
    bob, b_name = await reg_auth("bob")
    try:
        for i in range(3):
            await send_pm(alice, a_name, b_name, f"m{i}")

        summary = await get_unread(bob)
        conv = find_conv(summary, a_name)
        assert conv is not None, f"no conversation with {a_name}: {summary}"
        assert conv["count"] == 3, conv
        print(f"    ✓ summary: {conv['count']} unread from Alice")
    finally:
        await alice.close(); await bob.close()


async def t2_mark_read_clears_summary():
    alice, a_name = await reg_auth("alice")
    bob, b_name = await reg_auth("bob")
    try:
        await send_pm(alice, a_name, b_name, "hi")

        await bob.send({"type": "mark_read",
                        "data": {"with": a_name, "beforeMessageId": -1}})
        summary = await bob.expect("unread_summary")

        conv = find_conv(summary, a_name)
        assert conv is None, f"still unread after mark_read: {conv}"
        print("    ✓ unread count cleared after mark_read")
    finally:
        await alice.close(); await bob.close()


async def t3_receipt_delivered_to_sender():
    alice, a_name = await reg_auth("alice")
    bob, b_name = await reg_auth("bob")
    try:
        ack = await send_pm(alice, a_name, b_name, "hi")
        msg_id = ack["data"]["id"]

        await bob.send({"type": "mark_read",
                        "data": {"with": a_name, "beforeMessageId": -1}})
        await bob.expect("unread_summary")   # drain bob's auto refresh

        receipt = await alice.expect("read_receipt")
        assert receipt["data"]["from"] == b_name, receipt
        assert receipt["data"]["beforeMessageId"] == msg_id, receipt
        assert receipt["data"]["readAt"], receipt
        print(f"    ✓ receipt: Bob read Alice's msg {msg_id}")
    finally:
        await alice.close(); await bob.close()


async def t4_partial_mark_read():
    alice, a_name = await reg_auth("alice")
    bob, b_name = await reg_auth("bob")
    try:
        ids = []
        for i in range(5):
            ack = await send_pm(alice, a_name, b_name, f"m{i}")
            ids.append(ack["data"]["id"])

        # Mark first 3 (up to ids[2])
        await bob.send({"type": "mark_read",
                        "data": {"with": a_name, "beforeMessageId": ids[2]}})
        summary = await bob.expect("unread_summary")

        conv = find_conv(summary, a_name)
        assert conv is not None, "conv disappeared"
        assert conv["count"] == 2, f"expected 2 unread, got {conv}"
        print(f"    ✓ partial mark_read leaves {conv['count']} unread")
    finally:
        await alice.close(); await bob.close()


async def t5_receipt_ignored_when_nothing_unread():
    """mark_read on an already-read conversation should NOT send a receipt."""
    alice, a_name = await reg_auth("alice")
    bob, b_name = await reg_auth("bob")
    try:
        await send_pm(alice, a_name, b_name, "hi")

        # First read
        await bob.send({"type": "mark_read",
                        "data": {"with": a_name, "beforeMessageId": -1}})
        await bob.expect("unread_summary")
        receipt = await alice.expect("read_receipt")
        assert receipt["data"]["beforeMessageId"] > 0

        # Second read — nothing new
        await bob.send({"type": "mark_read",
                        "data": {"with": a_name, "beforeMessageId": -1}})
        await bob.expect("unread_summary")

        # Alice should NOT receive a second receipt
        try:
            frame = await alice.recv(timeout=1.5)
            if frame.get("type") == "read_receipt":
                raise AssertionError(f"spurious second receipt: {frame}")
        except asyncio.TimeoutError:
            pass
        print("    ✓ no duplicate receipt for already-read conversation")
    finally:
        await alice.close(); await bob.close()


async def t6_multi_session_receipt():
    """Bob has 2 sessions; Alice should only get ONE receipt."""
    alice, a_name = await reg_auth("alice")
    bob1, b_name = await reg_auth("bob-1")

    ws2 = await websockets.connect(SERVER_URI, open_timeout=DEFAULT_TIMEOUT, **connect_kwargs())
    bob2 = Client(ws2, "bob-2")
    await bob2.send({"type": "auth",
                     "data": {"username": b_name, "password": "testpass123"}})
    await bob2.expect("auth_success")

    try:
        await send_pm(alice, a_name, b_name, "hi")

        await bob2.send({"type": "mark_read",
                         "data": {"with": a_name, "beforeMessageId": -1}})
        await bob2.expect("unread_summary")

        receipt = await alice.expect("read_receipt")
        assert receipt["data"]["from"] == b_name
        print("    ✓ multi-session read triggers one receipt to sender")
    finally:
        await alice.close(); await bob1.close(); await bob2.close()


async def t7_unknown_user_mark_read():
    alice, _ = await reg_auth("alice")
    try:
        await alice.send({"type": "mark_read",
                          "data": {"with": "nonexistent_zzz", "beforeMessageId": -1}})
        err = await alice.expect("error")
        assert err["data"]["code"] == 404, err
        print("    ✓ unknown user in mark_read → 404")
    finally:
        await alice.close()


async def t8_empty_mark_read_rejected():
    alice, _ = await reg_auth("alice")
    try:
        await alice.send({"type": "mark_read",
                          "data": {"with": "", "beforeMessageId": -1}})
        err = await alice.expect("error")
        assert err["data"]["code"] == 400, err
        print("    ✓ empty partner in mark_read → 400")
    finally:
        await alice.close()


TESTS = [
    ("T1 summary counts received messages",   t1_summary_counts_received_messages),
    ("T2 mark_read clears summary",           t2_mark_read_clears_summary),
    ("T3 receipt delivered to sender",        t3_receipt_delivered_to_sender),
    ("T4 partial mark_read leaves remaining", t4_partial_mark_read),
    ("T5 no receipt when nothing unread",     t5_receipt_ignored_when_nothing_unread),
    ("T6 multi-session triggers one receipt", t6_multi_session_receipt),
    ("T7 unknown user in mark_read",          t7_unknown_user_mark_read),
    ("T8 empty partner rejected",             t8_empty_mark_read_rejected),
]


async def main():
    print("=" * 64)
    print(" Unread & Read Receipt Test Suite")
    print(f" Server: {SERVER_URI}")
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