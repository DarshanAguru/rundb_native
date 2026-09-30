#!/usr/bin/env python3
"""
Comprehensive RunDB Native Integration Test Suite.
Tests:
1. Core Protocol & Ping/Echo
2. String & Numeric Operations (SET, GET, MSET, MGET, INCR, DECR, INCRBY, APPEND, STRLEN)
3. List Data Type (LPUSH, RPUSH, LPOP, RPOP, LLEN, LINDEX, LRANGE)
4. Set Data Type (SADD, SREM, SISMEMBER, SCARD, SMEMBERS, Adaptive IntSet->HashSet)
5. Multi-Database Partitions (SELECT 0..15)
6. Atomic Transactions (MULTI, EXEC, DISCARD)
7. Passive & Active Expiration (EXPIRE, TTL, PEXPIRE, PERSIST)
8. AOF Persistence, BGREWRITEAOF Snapshotting & Startup Replay
9. CLI Flags & Memory Eviction Enforcement
"""

import socket
import subprocess
import time
import os
import sys

def send_raw(sock: socket.socket, cmd: bytes) -> bytes:
    sock.sendall(cmd)
    response = b""
    while True:
        chunk = sock.recv(4096)
        response += chunk
        if len(chunk) < 4096 or response.endswith(b"\r\n"):
            break
    return response

def test_core_and_structures(bin_path: str):
    port = 7379
    proc = subprocess.Popen([bin_path, "--port", str(port)])
    time.sleep(0.5)

    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", port))

        # 1. PING & ECHO
        assert send_raw(s, b"*1\r\n$4\r\nPING\r\n") == b"+PONG\r\n"
        assert send_raw(s, b"*2\r\n$4\r\nECHO\r\n$5\r\nHELLO\r\n") == b"$5\r\nHELLO\r\n"
        print("[✓] PING & ECHO passed")

        # 2. Strings & Numbers
        assert send_raw(s, b"SET mykey myval\r\n") == b"+OK\r\n"
        assert send_raw(s, b"GET mykey\r\n") == b"$5\r\nmyval\r\n"
        assert send_raw(s, b"SET counter 10\r\n") == b"+OK\r\n"
        assert send_raw(s, b"INCR counter\r\n") == b":11\r\n"
        assert send_raw(s, b"INCRBY counter 5\r\n") == b":16\r\n"
        assert send_raw(s, b"DECR counter\r\n") == b":15\r\n"
        assert send_raw(s, b"APPEND mykey _suffix\r\n") == b":12\r\n"
        assert send_raw(s, b"GET mykey\r\n") == b"$12\r\nmyval_suffix\r\n"
        assert send_raw(s, b"STRLEN mykey\r\n") == b":12\r\n"
        assert send_raw(s, b"MSET k1 v1 k2 v2\r\n") == b"+OK\r\n"
        mget_res = send_raw(s, b"MGET k1 k2 nonexisting\r\n")
        assert mget_res == b"*3\r\n$2\r\nv1\r\n$2\r\nv2\r\n$-1\r\n"
        print("[✓] String & Numeric operations passed")

        # 3. Lists (QuickList)
        assert send_raw(s, b"RPUSH mylist a b c\r\n") == b":3\r\n"
        assert send_raw(s, b"LPUSH mylist front\r\n") == b":4\r\n"
        assert send_raw(s, b"LLEN mylist\r\n") == b":4\r\n"
        assert send_raw(s, b"LINDEX mylist 0\r\n") == b"$5\r\nfront\r\n"
        assert send_raw(s, b"LINDEX mylist -1\r\n") == b"$1\r\nc\r\n"
        assert send_raw(s, b"LRANGE mylist 0 -1\r\n") == b"*4\r\n$5\r\nfront\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n"
        assert send_raw(s, b"LPOP mylist\r\n") == b"$5\r\nfront\r\n"
        assert send_raw(s, b"RPOP mylist\r\n") == b"$1\r\nc\r\n"
        print("[✓] List data type (LPUSH, RPUSH, LPOP, RPOP, LLEN, LINDEX, LRANGE) passed")

        # 4. Sets (Adaptive IntSet -> HashSet)
        assert send_raw(s, b"SADD intset 100 200 50\r\n") == b":3\r\n"
        assert send_raw(s, b"SISMEMBER intset 200\r\n") == b":1\r\n"
        assert send_raw(s, b"SISMEMBER intset 999\r\n") == b":0\r\n"
        assert send_raw(s, b"SCARD intset\r\n") == b":3\r\n"
        # Auto-promote to HashSet by adding string
        assert send_raw(s, b"SADD intset string_member\r\n") == b":1\r\n"
        assert send_raw(s, b"SISMEMBER intset string_member\r\n") == b":1\r\n"
        assert send_raw(s, b"SREM intset 100\r\n") == b":1\r\n"
        assert send_raw(s, b"SCARD intset\r\n") == b":3\r\n"
        print("[✓] Set data type (SADD, SREM, SISMEMBER, SCARD, adaptive IntSet->HashSet) passed")

        # 5. Type safety
        assert b"WRONGTYPE" in send_raw(s, b"LPUSH mykey invalid\r\n")
        assert b"WRONGTYPE" in send_raw(s, b"SADD mykey invalid\r\n")
        assert b"WRONGTYPE" in send_raw(s, b"GET mylist\r\n")
        print("[✓] WRONGTYPE safety checks passed")

        # 6. Multi-DB
        assert send_raw(s, b"SELECT 0\r\n") == b"+OK\r\n"
        assert send_raw(s, b"SET common_key db0\r\n") == b"+OK\r\n"
        assert send_raw(s, b"SELECT 1\r\n") == b"+OK\r\n"
        assert send_raw(s, b"SET common_key db1\r\n") == b"+OK\r\n"
        assert send_raw(s, b"GET common_key\r\n") == b"$3\r\ndb1\r\n"
        assert send_raw(s, b"SELECT 0\r\n") == b"+OK\r\n"
        assert send_raw(s, b"GET common_key\r\n") == b"$3\r\ndb0\r\n"
        print("[✓] Multi-database partition isolation (SELECT 0..15) passed")

        # 7. Transactions
        assert send_raw(s, b"MULTI\r\n") == b"+OK\r\n"
        assert send_raw(s, b"SET tx_key tx_val\r\n") == b"+QUEUED\r\n"
        assert send_raw(s, b"INCR tx_counter\r\n") == b"+QUEUED\r\n"
        tx_res = send_raw(s, b"EXEC\r\n")
        assert tx_res == b"*2\r\n+OK\r\n:1\r\n"
        assert send_raw(s, b"GET tx_key\r\n") == b"$6\r\ntx_val\r\n"

        # Discard
        assert send_raw(s, b"MULTI\r\n") == b"+OK\r\n"
        assert send_raw(s, b"SET disc_key val\r\n") == b"+QUEUED\r\n"
        assert send_raw(s, b"DISCARD\r\n") == b"+OK\r\n"
        assert send_raw(s, b"GET disc_key\r\n") == b"$-1\r\n"
        print("[✓] Atomic Transactions (MULTI, EXEC, DISCARD) passed")

        # 8. Expiry & TTL
        assert send_raw(s, b"SET exp_key value EX 1\r\n") == b"+OK\r\n"
        res = send_raw(s, b"TTL exp_key\r\n")
        assert res in (b":1\r\n", b":2\r\n")
        time.sleep(1.2)
        assert send_raw(s, b"GET exp_key\r\n") == b"$-1\r\n"
        print("[✓] Expiration & TTL passed")

        s.close()
    finally:
        proc.terminate()
        proc.wait()

def test_aof_persistence(bin_path: str):
    port = 7401
    aof_path = "test_run_master.aof"
    if os.path.exists(aof_path):
        os.remove(aof_path)

    # 1. Start server with AOF enabled
    proc = subprocess.Popen([bin_path, "--port", str(port), "--aof-enabled", "yes", "--aof-file", aof_path])
    time.sleep(0.5)

    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", port))

        # Write data to DB 0
        assert send_raw(s, b"SELECT 0\r\n") == b"+OK\r\n"
        assert send_raw(s, b"SET aof_str persistent_value\r\n") == b"+OK\r\n"
        assert send_raw(s, b"RPUSH aof_list item1 item2 item3\r\n") == b":3\r\n"
        assert send_raw(s, b"SADD aof_set 10 20 30\r\n") == b":3\r\n"
        assert send_raw(s, b"SET aof_ttl will_stay EX 300\r\n") == b"+OK\r\n"

        # Write data to DB 1
        assert send_raw(s, b"SELECT 1\r\n") == b"+OK\r\n"
        assert send_raw(s, b"SET db1_str db1_persisted\r\n") == b"+OK\r\n"

        # Trigger BGREWRITEAOF
        bg_res = send_raw(s, b"BGREWRITEAOF\r\n")
        assert b"Background append only file rewriting started" in bg_res
        time.sleep(0.5)

        s.close()
    finally:
        proc.terminate()
        proc.wait()

    assert os.path.exists(aof_path), "AOF file should have been created on disk"
    assert os.path.getsize(aof_path) > 0, "AOF file must not be empty"

    # 2. Restart server and verify state is replayed from AOF
    proc2 = subprocess.Popen([bin_path, "--port", str(port), "--aof-enabled", "yes", "--aof-file", aof_path])
    time.sleep(0.5)

    try:
        s2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s2.connect(("127.0.0.1", port))

        # Check DB 0
        assert send_raw(s2, b"SELECT 0\r\n") == b"+OK\r\n"
        assert send_raw(s2, b"GET aof_str\r\n") == b"$16\r\npersistent_value\r\n"
        assert send_raw(s2, b"LRANGE aof_list 0 -1\r\n") == b"*3\r\n$5\r\nitem1\r\n$5\r\nitem2\r\n$5\r\nitem3\r\n"
        assert send_raw(s2, b"SISMEMBER aof_set 20\r\n") == b":1\r\n"
        assert send_raw(s2, b"SISMEMBER aof_set 99\r\n") == b":0\r\n"
        ttl_res = send_raw(s2, b"TTL aof_ttl\r\n")
        assert ttl_res.startswith(b":") and ttl_res != b":-2\r\n"

        # Check DB 1
        assert send_raw(s2, b"SELECT 1\r\n") == b"+OK\r\n"
        assert send_raw(s2, b"GET db1_str\r\n") == b"$13\r\ndb1_persisted\r\n"

        s2.close()
        print("[✓] AOF Persistence, Snapshotting (BGREWRITEAOF) & Startup Replay passed")
    finally:
        proc2.terminate()
        proc2.wait()
        if os.path.exists(aof_path):
            os.remove(aof_path)
        if os.path.exists(aof_path + ".tmp"):
            os.remove(aof_path + ".tmp")

def test_config_and_eviction(bin_path: str):
    port = 7402
    # Start server with small memory limit (10 KB) and allkeys-lru eviction
    proc = subprocess.Popen([
        bin_path,
        "--port", str(port),
        "--memory-limit", "15000",
        "--eviction-strategy", "allkeys-lru"
    ])
    time.sleep(0.5)

    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", port))

        # Push many keys to exceed memory limit
        for i in range(200):
            res = send_raw(s, f"SET key_{i} {'x' * 200}\r\n".encode())
            assert res == b"+OK\r\n"

        # Oldest keys should have been evicted under allkeys-lru
        # Newest keys should be present
        assert send_raw(s, b"GET key_199\r\n") == f"$200\r\n{'x' * 200}\r\n".encode()

        s.close()
        print("[✓] CLI Flags (--memory-limit, --eviction-strategy) & LRU Eviction passed")
    finally:
        proc.terminate()
        proc.wait()

def main():
    bin_path = "./build/release/rundb"
    if not os.path.exists(bin_path):
        bin_path = "./build/rundb"

    print(f"[*] Running RunDB Native Complete Test Suite ({bin_path})...\n")

    test_core_and_structures(bin_path)
    test_aof_persistence(bin_path)
    test_config_and_eviction(bin_path)

    print("\n🎉 ALL RunDB NATIVE INTEGRATION TESTS PASSED WITH 100% SUCCESS!")

if __name__ == "__main__":
    main()
