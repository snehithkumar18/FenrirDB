import os
import struct

os.makedirs("fenrirdb/fuzz/corpus/fuzz_query", exist_ok=True)
os.makedirs("fenrirdb/fuzz/corpus/fuzz_storage", exist_ok=True)

# Query seeds
queries = [
    b"age > 21",
    b"name = Alice",
    b"active = true",
    b"name = 12345",
]
for i, q in enumerate(queries):
    with open(f"fenrirdb/fuzz/corpus/fuzz_query/seed_{i}.txt", "wb") as f:
        f.write(q)

# Storage seeds: a valid page structure (4096 bytes)
# Header: page_id=0, num_records=2, free_space_pointer=4000
# Slots:
# Slot 0: offset=4050, length=46
# Slot 1: offset=4000, length=50
page_data = bytearray(4096)
struct.pack_into("<I", page_data, 0, 0)
struct.pack_into("<H", page_data, 4, 2)
struct.pack_into("<H", page_data, 6, 4000)

# Slot 0: offset=4050, length=18
struct.pack_into("<H", page_data, 8, 4050)
struct.pack_into("<H", page_data, 10, 18)

# Slot 1: offset=4000, length=12
struct.pack_into("<H", page_data, 12, 4000)
struct.pack_into("<H", page_data, 14, 12)

# Write records data at offsets
# Record 0 (at 4050, len 18): serialized Document with 1 field "name" -> Variant("Alice")
# num_fields=1 (2B), key_len=4 (2B), key="name" (4B), type=2 (1B), val_len=5 (2B), val="Alice" (5B) -> Total 16B
doc0 = struct.pack("<HH4sBH5s", 1, 4, b"name", 2, 5, b"Alice")
page_data[4050:4050+len(doc0)] = doc0

# Record 1 (at 4000, len 12): serialized Document with 1 field "age" -> Variant(30)
# num_fields=1 (2B), key_len=3 (2B), key="age" (3B), type=1 (1B), val=30 (4B) -> Total 12B
doc1 = struct.pack("<HH3sBI", 1, 3, b"age", 1, 30)
page_data[4000:4000+len(doc1)] = doc1

with open("fenrirdb/fuzz/corpus/fuzz_storage/seed_0.bin", "wb") as f:
    f.write(page_data)

# Another seed: an empty page
empty_page = bytearray(4096)
struct.pack_into("<I", empty_page, 0, 1)
struct.pack_into("<H", empty_page, 4, 0)
struct.pack_into("<H", empty_page, 6, 4096)

with open("fenrirdb/fuzz/corpus/fuzz_storage/seed_1.bin", "wb") as f:
    f.write(empty_page)

print("Generated fuzzing seed files.")
