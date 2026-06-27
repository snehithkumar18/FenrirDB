import struct
import os

def write_seed(directory, filename, data):
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, filename)
    with open(path, "wb") as f:
        f.write(data)
    print(f"Generated seed: {path} ({len(data)} bytes)")

# Seeds for fuzz_query
def gen_query_seeds():
    d = "fuzz/corpus/fuzz_query"
    
    # Seed 1: Simple SELECT
    write_seed(d, "seed_select.bin", b"SELECT name FROM users WHERE age > 25")
    
    # Seed 2: INSERT
    write_seed(d, "seed_insert.bin", b'INSERT INTO users VALUES {"name":"alice","age":30}')
    
    # Seed 3: DELETE
    write_seed(d, "seed_delete.bin", b"DELETE FROM users WHERE name = 'bob'")
    
    # Seed 4: JOIN
    write_seed(d, "seed_join.bin", b"SELECT * FROM users JOIN orders ON users.id = orders.user_id")
    
    # Seed 5: Aggregation
    write_seed(d, "seed_agg.bin", b"SELECT dept, COUNT(*) FROM employees GROUP BY dept")

# Seeds for fuzz_storage
def gen_storage_seeds():
    d = "fuzz/corpus/fuzz_storage"
    
    # Seed 1: Valid page with 1 record
    page = bytearray(4096)
    struct.pack_into("<H", page, 4, 1)  # num_records = 1
    struct.pack_into("<H", page, 6, 4080)  # free_ptr
    struct.pack_into("<HH", page, 8, 4080, 16)  # slot: offset=4080, len=16
    page[4080:4096] = b"hello_record_001"
    write_seed(d, "seed_page_1rec.bin", bytes(page))
    
    # Seed 2: Valid page with 3 records
    page2 = bytearray(4096)
    struct.pack_into("<H", page2, 4, 3)
    struct.pack_into("<H", page2, 6, 4060)
    struct.pack_into("<HH", page2, 8, 4084, 12)
    struct.pack_into("<HH", page2, 12, 4072, 12)
    struct.pack_into("<HH", page2, 16, 4060, 12)
    page2[4060:4072] = b"record_three"
    page2[4072:4084] = b"record__two_"
    page2[4084:4096] = b"record__one_"
    write_seed(d, "seed_page_3rec.bin", bytes(page2))
    
    # Seed 3: Empty page
    page3 = bytearray(4096)
    struct.pack_into("<H", page3, 4, 0)
    struct.pack_into("<H", page3, 6, 4096)
    write_seed(d, "seed_page_empty.bin", bytes(page3))

if __name__ == "__main__":
    gen_query_seeds()
    gen_storage_seeds()
    print("\nAll seed corpus files generated!")
