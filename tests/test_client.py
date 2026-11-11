import asyncio
import websockets
import json
from datetime import datetime

async def test_chat_server():
    uri = "ws://localhost:8080"

    print(f"Connecting to {uri}...")

    try:
        async with websockets.connect(uri) as websocket:
            print("✓ Connected to chat server")

            # Test 1: Authentication with demo user
            print("\n--- Test 1: Authentication ---")
            auth_msg = {
                "type": "auth",
                "data": {
                    "username": "demo",
                    "password": "demo123"
                }
            }
            await websocket.send(json.dumps(auth_msg))
            response = await websocket.recv()
            print(f"Auth response: {response}")

            # Test 2: Send a chat message
            print("\n--- Test 2: Send Chat Message ---")
            message = {
                "type": "message",
                "data": {
                    "roomId": 1,
                    "content": "Hello from Python client!"
                }
            }
            await websocket.send(json.dumps(message))

            # Wait for potential broadcast (might not receive if no other clients)
            try:
                response = await asyncio.wait_for(websocket.recv(), timeout=2.0)
                print(f"Message response: {response}")
            except asyncio.TimeoutError:
                print("No response (expected if no other clients in room)")

            # Test 3: Request history
            print("\n--- Test 3: Request History ---")
            history_msg = {
                "type": "history_request",
                "data": {
                    "roomId": 1,
                    "limit": 10
                }
            }
            await websocket.send(json.dumps(history_msg))
            response = await websocket.recv()
            print(f"History response: {response}")

            # Test 4: Join a room
            print("\n--- Test 4: Join Room ---")
            join_msg = {
                "type": "join_room",
                "data": {
                    "roomId": 2
                }
            }
            await websocket.send(json.dumps(join_msg))
            response = await websocket.recv()
            print(f"Join response: {response}")

            # Test 5: Invalid authentication
            print("\n--- Test 5: Invalid Auth ---")
            bad_auth = {
                "type": "auth",
                "data": {
                    "username": "wrong",
                    "password": "wrong"
                }
            }
            await websocket.send(json.dumps(bad_auth))
            response = await websocket.recv()
            print(f"Error response: {response}")

            print("\n✓ All tests completed!")

    except websockets.exceptions.ConnectionClosed:
        print("✗ Connection closed by server")
    except Exception as e:
        print(f"✗ Error: {e}")

if __name__ == "__main__":
    print("Chat Server Test Client")
    print("=" * 40)
    asyncio.run(test_chat_server())