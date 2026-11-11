import asyncio
import websockets
import json

async def test_realtime_chat():
    uri = "ws://localhost:8080"

    print("Connecting two clients...")

    # Connect both clients
    client1 = await websockets.connect(uri)
    client2 = await websockets.connect(uri)

    # Both authenticate as demo (since that's the only user)
    auth = {"type": "auth", "data": {"username": "demo", "password": "demo123"}}

    await client1.send(json.dumps(auth))
    response1 = await client1.recv()
    print(f"Client 1 authenticated: {response1}")

    await client2.send(json.dumps(auth))
    response2 = await client2.recv()
    print(f"Client 2 authenticated: {response2}")

    # Both join room 1
    join = {"type": "join_room", "data": {"roomId": 1}}
    await client1.send(json.dumps(join))
    await client1.recv()

    await client2.send(json.dumps(join))
    await client2.recv()

    # Test 1: Client 1 sends message
    print("\n--- Client 1 sends a message ---")
    msg1 = {"type": "message", "data": {"roomId": 1, "content": "Hello from Client 1!"}}
    await client1.send(json.dumps(msg1))

    # Client 2 should receive Client 1's message
    response = await asyncio.wait_for(client2.recv(), timeout=5.0)
    print(f"Client 2 received: {response}")

    # Test 2: Client 2 sends message
    print("\n--- Client 2 sends a message ---")
    msg2 = {"type": "message", "data": {"roomId": 1, "content": "Hello from Client 2!"}}
    await client2.send(json.dumps(msg2))

    # Client 1 should receive Client 2's message
    response = await asyncio.wait_for(client1.recv(), timeout=5.0)
    print(f"Client 1 received: {response}")

    # Verify messages are different
    if "Client 1" in response:
        print("❌ Wrong message received!")
    elif "Client 2" in response:
        print("✅ Correct message received!")

    await client1.close()
    await client2.close()
    print("\n✅ Real-time messaging test complete!")

asyncio.run(test_realtime_chat())