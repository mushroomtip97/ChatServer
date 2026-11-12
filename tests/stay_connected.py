import asyncio, json, uuid, websockets, ssl

async def main():
    ctx = ssl.create_default_context()
    ctx.load_verify_locations(r"C:\Users\PC\QtProjects\chat-server-project\chat-server\certs\server.crt")

    async with websockets.connect("wss://localhost:8080", ssl=ctx) as ws:
        u = f"metric_{uuid.uuid4().hex[:6]}"
        await ws.send(json.dumps({"type": "register", "data": {"username": u, "password": "testpass123"}}))
        print("register:", await ws.recv())
        await ws.send(json.dumps({"type": "auth", "data": {"username": u, "password": "testpass123"}}))
        print("auth:", await ws.recv())

        await ws.send(json.dumps({"type": "join_room", "data": {"roomId": 1}}))
        await ws.recv()
        await ws.send(json.dumps({"type": "message", "data": {"roomId": 1, "content": "hello"}}))
        await asyncio.sleep(0.3)

        await ws.send(json.dumps({
            "type": "private_message",
            "data": {"to": "demo", "content": "test"}
        }))

        print("Keeping connection open. Press Ctrl+C to stop.")
        # Keep the connection alive; respond to pings, ignore everything else.
        while True:
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=1.0)
                print("received:", msg)
            except asyncio.TimeoutError:
                pass

asyncio.run(main())
