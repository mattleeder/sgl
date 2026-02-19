# Tests require that sqlite3.exe is on PATH

import subprocess
import time

RED = "\033[31m"
GREEN = "\033[32m"
RESET = "\033[0m"

TEST_QUERIES = [
    ["companies.db",    ".tables"],
    ["companies.db",    ".dbinfo"],
    ["companies.db",    "SELECT id, name FROM companies WHERE country = 'north korea'"],
    ["superheroes.db",  "SELECT id, name FROM superheroes WHERE hair_color = 'Gold Hair'"],
    ["companies.db",    "SELECT id, name FROM companies WHERE country = 'chad'"],
    ["companies.db",    "SELECT id, name FROM companies WHERE country = 'eritrea'"],
    ["companies.db",    "SELECT id, name FROM companies WHERE country = 'chad'"],
    ["companies.db",    "SELECT id, name FROM companies WHERE country = 'republic of the congo'"],
]

def print_result(
        result_one: subprocess.CompletedProcess,
        result_two: subprocess.CompletedProcess,
        test_num: int,
        db_name: str,
        query_string: str,
        our_engine_elapsed_time: float,
        sqlite_elapsed_time: float
        ):
    
    if result_one.stdout == result_two.stdout:
        print(f"{GREEN}Test {test_num} succeeded{RESET}. Time taken: {our_engine_elapsed_time:.4g}ms, SQLite time taken: {sqlite_elapsed_time:.4g}ms")
    else:
        print(f"{RED}Test {test_num} failed{RESET}. DB Name: {db_name}, Query: {query_string}")


def run_tests():
    print(f"Running {len(TEST_QUERIES)} tests")
    for test_num, (db_name, query) in enumerate(TEST_QUERIES, start = 1):
        sqlite_start_time = time.monotonic()
        sqlite = subprocess.run(["sqlite3.exe", db_name, query], capture_output = True)
        sqlite_end_time = time.monotonic()

        our_engine_start_time = time.monotonic()
        our_engine = subprocess.run(["sql.exe", db_name, query], capture_output = True)
        our_engine_end_time = time.monotonic()

        print_result(
            sqlite,
            our_engine,
            test_num,
            db_name,
            query,
            our_engine_end_time - our_engine_start_time,
            sqlite_end_time - sqlite_start_time
            )

def main():
    run_tests()
    return 0

if __name__ == "__main__":
    main()