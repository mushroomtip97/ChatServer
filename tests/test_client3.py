import asyncio
import websockets
import json
import time

async def test_registration():
    uri = "ws://localhost:8080"
    unique_username = f"user_{int(time.time())}"

    async with websockets.connect(uri) as websocket:
        print(f"--- Register New User ({unique_username}) ---")
        register_msg = {
            "type": "register",
            "data": {
                "username": unique_username,
                "password": "password123"
            }
        }
        await websocket.send(json.dumps(register_msg))
        response = await websocket.recv()
        print(f"Register: {response}")

        print(f"\n--- Authenticate {unique_username} ---")
        auth_msg = {
            "type": "auth",
            "data": {
                "username": unique_username,
                "password": "password123"
            }
        }
        await websocket.send(json.dumps(auth_msg))
        response = await websocket.recv()
        print(f"Auth: {response}")

asyncio.run(test_registration())