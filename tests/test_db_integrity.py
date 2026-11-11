import sqlite3
conn = sqlite3.connect(r"C:\Users\PC\QtProjects\chat-server-project\build-debug\chat_server.db")
cur = conn.cursor()
cur.execute("""
    SELECT u.id, u.username FROM users u
    LEFT JOIN room_members rm ON rm.user_id = u.id
    WHERE rm.user_id IS NULL
""")
orphans = cur.fetchall()
assert not orphans, f"Users without room membership: {orphans}"
print("✓ No orphan users — registration transactions are working")
conn.close()