import asyncio
import json
import ssl
import websockets

async def main():
    ctx = ssl.create_default_context()
    ctx.load_verify_locations(
        r"C:\Users\PC\QtProjects\chat-server-project\chat-server\certs\server.crt"
    )

    async with websockets.connect("wss://localhost:8080", ssl=ctx) as ws:
        await ws.send(json.dumps({
            "type": "auth",
            "data": {"username": "nobody", "password": "wrong"}
        }))
        print(await ws.recv())

asyncio.run(main())