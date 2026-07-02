#!/usr/bin/env python3
from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parent


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        data = data.encode("utf-8")
    path.write_bytes(data)


def payload_escape(data):
    out = bytearray()
    for b in data:
        if b in (0x00, 0xFF):
            out.append(0xFF)
            out.append(b ^ 0x20)
        else:
            out.append(b)
    return bytes(out)


def op(tag, flags, arg0, arg1, name, payload):
    name_b = name.encode("utf-8")
    payload_b = payload_escape(payload)
    return (
        bytes([tag])
        + struct.pack("<HIIHI", flags, arg0, arg1, len(name_b), len(payload_b))
        + name_b
        + payload_b
    )


def workload(ops):
    return b"FDBW" + struct.pack("<HH", 1, len(ops)) + b"".join(ops)


write(ROOT / "corpus" / "fuzz_expression" / "seed_filter.expr", "score > 10")
write(ROOT / "corpus" / "fuzz_expression" / "seed_call.expr", "contains(region,'we')")
write(ROOT / "corpus" / "fuzz_expression" / "seed_bucket.expr", "bucket(score,7) > 2")

write(
    ROOT / "corpus" / "fuzz_manifest" / "seed_manifest.txt",
    "[base]\nshards=2\nreplicas=2\nmode=primary\n[analytics]\nparent=base\nshards=3\n",
)
write(
    ROOT / "corpus" / "fuzz_manifest" / "seed_manifest_binary.bin",
    struct.pack("<H", 1)
    + bytes([7, 2, 1])
    + struct.pack("<H", 1)
    + b"default"
    + bytes([4, 7])
    + b"modeprimary",
)

write(
    ROOT / "corpus" / "fuzz_pipeline" / "seed_pipeline.txt",
    "load|users\nfilter|score > 10\nproject|id,score,region\nwindow|group|rows|3\npreview|gray\n",
)
write(
    ROOT / "corpus" / "fuzz_pipeline" / "seed_pipeline_binary.bin",
    struct.pack("<H", 3)
    + bytes([1, 5, 0])
    + struct.pack("<h", 0)
    + b"users"
    + bytes([2, 10, 0])
    + struct.pack("<h", 0)
    + b"score > 5"
    + bytes([6, 4, 0])
    + struct.pack("<h", 0)
    + b"gray",
)

write(
    ROOT / "corpus" / "fuzz_segment_cache" / "seed_cache.bin",
    bytes([3, 0, 1, 4, 5]) + b"users" + bytes([1, 2, 3, 4])
    + bytes([1, 1, 0, 5]) + b"users"
    + bytes([2, 1, 2, 5]) + b"users" + bytes([0, 0]),
)

write(
    ROOT / "corpus" / "fuzz_workload" / "seed_workload.bin",
    workload(
        [
            op(1, 0, 0, 0, "manifest", b"[default]\nshards=2\nreplicas=1\n"),
            op(2, 0, 0, 0, "expr", b"score > 10"),
            op(3, 0, 0, 0, "gray", b"load|users\nfilter|score > 10\npreview|gray\n"),
            op(4, 0, 0, 0, "cache", bytes([0, 1, 3, 5]) + b"usersabc"),
            op(5, 0, 0, 0, "roundtrip", b""),
        ]
    ),
)

write(
    ROOT / "corpus" / "fuzz_catalog_planner" / "seed_catalog_workload.txt",
    "table users\n"
    "version 1\n"
    "field id int required unique min=0\n"
    "field email string required maxlen=120\n"
    "field score int default=0 min=0 max=1000\n"
    "field active bool default=true\n"
    "index users_pk btree id unique\n"
    "index users_email hash email sparse include=id,active\n"
    "---\n"
    "insert|users|doc=id=7,email='a@example.test',score=42\n"
    "find|users|where=id=7|project=id,email\n"
    "update|users|where=email='a@example.test'|set=score=99\n",
)

write(
    ROOT / "corpus" / "fuzz_schema_evolution" / "seed_migration.txt",
    "table users\n"
    "version 1\n"
    "field id int required unique min=0\n"
    "field email string required maxlen=120\n"
    "field score int default=0 min=0 max=1000\n"
    "index users_pk btree id unique\n"
    "---\n"
    "step users 1 2\n"
    "action add_field active bool default=true\n"
    "action add_index users_score score\n",
)

write(
    ROOT / "corpus" / "fuzz_replication_topology" / "seed_topology.txt",
    "node n1 zone-a rack-1 primary healthy capacity=100 used=50 tag=ssd\n"
    "node n2 zone-b rack-2 secondary healthy capacity=100 used=25 tag=ssd\n"
    "node n3 zone-c rack-3 secondary healthy capacity=100 used=10 tag=hdd\n"
    "node n4 zone-a rack-4 secondary offline capacity=100 used=80\n"
    "shard s1 5 n1:leader:log=100 n2:log=99 n3:log=98\n"
    "shard s2 8 n4:leader:log=80 n2:log=80\n",
)

write(
    ROOT / "corpus" / "fuzz_checkpoint_scheduler" / "seed_policy.txt",
    "max_wal_bytes=1024\n"
    "max_dirty_pages=100\n"
    "periodic_interval_ms=5000\n"
    "min_interval_ms=100\n"
    "max_replica_lag=50\n"
    "full_every_n=2\n"
    "allow_during_backup=true\n"
    "prefer_incremental=true\n",
)

write(
    ROOT / "corpus" / "fuzz_backup_restore" / "seed_backup_manifest.txt",
    "table users\n"
    "version 1\n"
    "field id int required unique min=0\n"
    "field email string required maxlen=120\n"
    "field score int default=0 min=0 max=1000\n"
    "index users_pk btree id unique\n"
    "---\n"
    "backup b001 full start=1000 end=2000 checkpoint=1 wal=0-500 catalog=abc\n"
    "object b001/catalog catalog - 256 checksum=c1\n"
    "object b001/table/users table users 4096 checksum=t1\n"
    "object b001/index/users/users_pk index users 1024 checksum=i1\n"
    "object b001/wal wal - 500 checksum=w1\n"
    "backup b002 incremental start=3000 end=4000 checkpoint=2 wal=500-900 catalog=abc parent=b001\n"
    "object b002/table/users table users 2048 checksum=t2\n"
    "object b002/wal wal - 400 checksum=w2\n",
)

write(
    ROOT / "corpus" / "fuzz_telemetry" / "seed_telemetry.txt",
    "counter queries_total 3 table=users 100\n"
    "counter queries_total 2 table=events 101\n"
    "gauge cache_pages 50 pool=main 103\n"
    "histogram query_ms 7.5 1,10,100 104\n"
    "histogram query_ms 80 1,10,100 105\n"
    "span query 0 1000 1210 sql=select\n"
    "span scan 1 1010 1200 table=users\n",
)


def varint(value):
    out = bytearray()
    while value >= 0x80:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value & 0x7F)
    return bytes(out)


def fnv(payload):
    h = 2166136261
    for b in payload:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def stream_escape(payload):
    out = bytearray()
    for b in payload:
        if b in (0x7E, 0x7D):
            out.append(0x7D)
            out.append(b ^ 0x20)
        else:
            out.append(b)
    return bytes(out)


def frame(ftype, flags, seq, name, payload):
    name_b = name.encode("utf-8")
    escaped = stream_escape(payload)
    return (
        bytes([ftype])
        + struct.pack("<H", flags)
        + varint(seq)
        + varint(len(name_b))
        + varint(len(escaped))
        + struct.pack("<I", fnv(payload))
        + name_b
        + escaped
    )


write(
    ROOT / "corpus" / "fuzz_binary_stream" / "seed_stream.bin",
    b"FDBS"
    + varint(2)
    + frame(3, 0, 0, "catalog", b"table users")
    + frame(2, 0, 1, "wal", bytes([0x7E, 0x7D, 1, 2, 3])),
)

write(
    ROOT / "corpus" / "fuzz_batch_job_runner" / "seed_batch.txt",
    "table users\n"
    "version 1\n"
    "field id int required unique min=0\n"
    "field email string required maxlen=120\n"
    "field score int default=0 min=0 max=1000\n"
    "index users_pk btree id unique\n"
    "---\n"
    "job nightly\n"
    "task validate validate_catalog cost=1\n"
    "task plan plan_workload deps=validate cost=3 workload=find|users|where=id=7|project=id,email\n"
    "task checkpoint run_checkpoint deps=plan cost=2 wal=2048 dirty=64\n"
    "task metrics emit_telemetry deps=checkpoint cost=1\n",
)

write(
    ROOT / "corpus" / "fuzz_data_transform" / "seed_transform.txt",
    "project=id,region|filter=id>3|limit=8",
)
