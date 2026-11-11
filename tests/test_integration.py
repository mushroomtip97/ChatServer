"""
Edit/delete message integration test.

Two clients join a room. Client A sends a message; client B receives
the broadcast and provides the message ID. A then edits/deletes, and
both clients observe the resulting broadcast.
"""

import asyncio, json, sys, time, uuid, websockets
from tls_helper import SERVER_URI, connect_kwargs

DEFAULT_TIMEOUT = 5.0


class Client:
    def __init__(self, ws, name):
        self.ws = ws
        self.name = name

    async def send(self, o):
        await self.ws.send(json.dumps(o))

    async def recv(self, timeout=DEFAULT_TIMEOUT):
        return json.loads(await asyncio.wait_for(self.ws.recv(), timeout=timeout))

    async def expect(self, t, timeout=DEFAULT_TIMEOUT):
        deadline = time.monotonic() + timeout
        skipped = []
        while time.monotonic() < deadline:
            frame = await self.recv(timeout=max(0.01, deadline - time.monotonic()))
            if frame.get("type") == t:
                return frame
            skipped.append(frame.get("type"))
        raise AssertionError(f"[{self.name}] never saw '{t}'; skipped={skipped[:5]}")

    async def close(self):
        try: await self.ws.close()
        except Exception: pass


async def reg_auth(name):
    u = f"ed_{uuid.uuid4().hex[:8]}"
    ws = await websockets.connect(SERVER_URI, open_timeout=DEFAULT_TIMEOUT, **connect_kwargs())
    c = Client(ws, name)
    await c.send({"type": "register", "data": {"username": u, "password": "testpass123"}})
    await c.expect("register_success")
    await c.send({"type": "auth", "data": {"username": u, "password": "testpass123"}})
    await c.expect("auth_success")
    return c, u


async def join(c, room_id):
    await c.send({"type": "join_room", "data": {"roomId": room_id}})
    await c.expect("room_joined")


async def send_and_observe(sender, observer, room_id, content):
    """Sender sends; observer receives and returns the message frame (has the id)."""
    await sender.send({"type": "message", "data": {"roomId": room_id, "content": content}})
    return await observer.expect("message")


# ---------------------------------------------------------------------------

async def t1_author_edits():
    """Author edits; both clients see the edited broadcast."""
    a, _ = await reg_auth("a")
    b, _ = await reg_auth("b")
    ROOM = 9101
    try:
        await join(a, ROOM)
        await join(b, ROOM)
        await asyncio.sleep(0.2)

        # A sends, B receives and provides the id
        msg = await send_and_observe(a, b, ROOM, "original")
        msg_id = msg["data"]["id"]

        # A edits
        await a.send({"type": "edit_message", "data": {"id": msg_id, "content": "updated"}})

        e1 = await a.expect("message_edited")
        e2 = await b.expect("message_edited")
        assert e1["data"]["id"] == msg_id
        assert e1["data"]["content"] == "updated"
        assert e2["data"]["content"] == "updated"
        print("    ✓ both clients saw the edit broadcast")
    finally:
        await a.close(); await b.close()


async def t2_non_author_rejected():
    """Non-author cannot edit or delete."""
    a, _ = await reg_auth("a")
    b, _ = await reg_auth("b")
    ROOM = 9102
    try:
        await join(a, ROOM)
        await join(b, ROOM)
        await asyncio.sleep(0.2)

        msg = await send_and_observe(a, b, ROOM, "mine")
        msg_id = msg["data"]["id"]

        # B tries to edit A's message
        await b.send({"type": "edit_message", "data": {"id": msg_id, "content": "hacked"}})
        err = await b.expect("error")
        assert err["data"]["code"] == 403, err

        # B tries to delete
        await b.send({"type": "delete_message", "data": {"id": msg_id}})
        err = await b.expect("error")
        assert err["data"]["code"] == 403, err

        print("    ✓ non-author rejected with 403")
    finally:
        await a.close(); await b.close()


async def t3_author_deletes():
    """Author deletes; both see the deleted broadcast."""
    a, _ = await reg_auth("a")
    b, _ = await reg_auth("b")
    ROOM = 9103
    try:
        await join(a, ROOM)
        await join(b, ROOM)
        await asyncio.sleep(0.2)

        msg = await send_and_observe(a, b, ROOM, "to be removed")
        msg_id = msg["data"]["id"]

        await a.send({"type": "delete_message", "data": {"id": msg_id}})

        d1 = await a.expect("message_deleted")
        d2 = await b.expect("message_deleted")
        assert d1["data"]["id"] == msg_id
        assert d2["data"]["id"] == msg_id
        print("    ✓ both clients saw the delete broadcast")
    finally:
        await a.close(); await b.close()


async def t4_edit_deleted_rejected():
    """Editing a deleted message returns 400."""
    a, _ = await reg_auth("a")
    b, _ = await reg_auth("b")   # second client to observe the initial message
    ROOM = 9104
    try:
        await join(a, ROOM)
        await join(b, ROOM)
        await asyncio.sleep(0.2)

        msg = await send_and_observe(a, b, ROOM, "x")
        msg_id = msg["data"]["id"]

        await a.send({"type": "delete_message", "data": {"id": msg_id}})
        await a.expect("message_deleted")

        await a.send({"type": "edit_message", "data": {"id": msg_id, "content": "y"}})
        err = await a.expect("error")
        assert err["data"]["code"] == 400, err
        print("    ✓ edit-after-delete rejected with 400")
    finally:
        await a.close(); await b.close()


async def t5_history_reflects_changes():
        """History returns edited content and the deleted marker."""
        a, _ = await reg_auth("a")
        b, _ = await reg_auth("b")
        ROOM = 9105
        try:
            await join(a, ROOM)
            await join(b, ROOM)
            await asyncio.sleep(0.2)

            msg1 = await send_and_observe(a, b, ROOM, "first")
            msg2 = await send_and_observe(a, b, ROOM, "second")

            id1 = msg1["data"]["id"]
            id2 = msg2["data"]["id"]
            print(f"    sent ids: id1={id1} id2={id2}")

            # Edit msg1, delete msg2
            await a.send({"type": "edit_message", "data": {"id": id1, "content": "first-edited"}})
            await a.expect("message_edited")
            await a.send({"type": "delete_message", "data": {"id": id2}})
            await a.expect("message_deleted")

            await a.send({"type": "history_request", "data": {"roomId": ROOM, "limit": 100}})
            hist = await a.expect("history_response")
            by_id = {m["id"]: m for m in hist["data"]["messages"]}

            print(f"    history returned {len(by_id)} messages, ids: {sorted(by_id.keys())}")
            print(f"    id1 entry: {by_id.get(id1)}")
            print(f"    id2 entry: {by_id.get(id2)}")

            assert id1 in by_id, f"id1={id1} not in history"
            assert id2 in by_id, f"id2={id2} not in history"

            assert by_id[id1]["content"] == "first-edited", \
                f"expected 'first-edited', got '{by_id[id1].get('content')}'"
            assert "editedAt" in by_id[id1], \
                f"editedAt missing from: {by_id[id1]}"
            assert "deletedAt" in by_id[id2], \
                f"deletedAt missing from: {by_id[id2]}"

            print("    ✓ history reflects edited + deleted state")
        finally:
            await a.close(); await b.close()


TESTS = [
    ("T1 author edits",                t1_author_edits),
    ("T2 non-author rejected",         t2_non_author_rejected),
    ("T3 author deletes",              t3_author_deletes),
    ("T4 edit-after-delete rejected",  t4_edit_deleted_rejected),
    ("T5 history reflects changes",    t5_history_reflects_changes),
]


async def main():
    print("=" * 60)
    print(" Edit/Delete Message Test Suite")
    print("=" * 60)

    try:
        ws = await websockets.connect(SERVER_URI, open_timeout=3.0, **connect_kwargs())
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

    print("\n" + "=" * 60)
    print(f"SUMMARY: {passed}/{len(TESTS)} passed, {failed} failed")
    for name, reason in failures:
        print(f"  • {name}: {reason}")
    print("=" * 60)
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    asyncio.run(main())