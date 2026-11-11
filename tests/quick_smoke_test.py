import asyncio, json, uuid, websockets, time

from tls_helper import SERVER_URI, connect_kwargs

DEFAULT_TIMEOUT = 5.0

async def recv_expect(ws, want, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = await asyncio.wait_for(ws.recv(), timeout=deadline - time.monotonic())
        obj = json.loads(raw)
        if obj.get("type") == want:
            return obj
    raise AssertionError(f"never saw '{want}'")

async def reg(name):
    ws = await websockets.connect(SERVER_URI, open_timeout=DEFAULT_TIMEOUT, **connect_kwargs())
    u = f"sm_{uuid.uuid4().hex[:6]}"
    await ws.send(json.dumps({"type":"register","data":{"username":u,"password":"pw123456"}}))
    await recv_expect(ws, "register_success")
    await ws.send(json.dumps({"type":"auth","data":{"username":u,"password":"pw123456"}}))
    await recv_expect(ws, "auth_success")
    return ws, u

async def main():
    a_ws, a = await reg("alice")
    b_ws, b = await reg("bob")

    # Alice sends 2 PMs to Bob
    for i in range(2):
        await a_ws.send(json.dumps({"type":"private_message","data":{"to":b,"content":f"hi{i}"}}))
        ack = await recv_expect(a_ws, "private_message_sent")
        print(f"alice ack {i}:", ack)

    # Bob asks for unread summary
    await b_ws.send(json.dumps({"type":"unread_request","data":{}}))
    summary = await recv_expect(b_ws, "unread_summary")
    print("bob summary:", summary)

    # Bob marks read
    await b_ws.send(json.dumps({"type":"mark_read","data":{"with":a,"beforeMessageId":-1}}))
    updated = await recv_expect(b_ws, "unread_summary")
    print("bob after mark_read:", updated)

    receipt = await recv_expect(a_ws, "read_receipt")
    print("alice got receipt:", receipt)

    await a_ws.close()
    await b_ws.close()

asyncio.run(main())