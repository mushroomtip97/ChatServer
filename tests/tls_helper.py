import asyncio
import json
import ssl
import uuid
import websockets



"""
Shared TLS helper for all test files.

Change CHAT_SERVER_URI to ws://localhost:8080 to run tests in plaintext mode.
"""

import ssl

SERVER_URI = "wss://localhost:8080"
CERT_PATH = r"C:\Users\PC\QtProjects\chat-server-project\chat-server\certs\server.crt"


def make_ssl_context():
    """Return an SSL context that trusts our self-signed cert."""
    ctx = ssl.create_default_context()
    ctx.load_verify_locations(CERT_PATH)
    return ctx


def connect_kwargs():
    """Pass to websockets.connect as **connect_kwargs()."""
    if SERVER_URI.startswith("wss://"):
        return {"ssl": make_ssl_context()}
    return {}