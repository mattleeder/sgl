# SGL

A file-based SQL engine written in C. Stores data in the SQLite on-disk format — a B-tree arranged into fixed-size pages — and executes queries using a volcano-style iterator model.

![C](https://img.shields.io/badge/language-C-lightgrey)

## What it does

- Parses and executes `SELECT` with `WHERE` filtering
- Chooses between full-table scans and index scans depending on the query
- Supports `COUNT` as an aggregate operation
- Reads `.db` files compatible with the SQLite page format

## Why I built it

I used SQL heavily as a data analyst and wanted to understand what actually happens below the query — how data is laid out on disk, how a parser turns text into something executable, and how query engines pipeline results without materialising entire intermediate tables. I also wanted to write cleaner parser code than I had in previous projects.

# Getting started

## Clone and build (requires gcc or clang)
`git clone https://github.com/mattleeder/sgl.git`
`cd sgl`
`gcc -g -O0 -Wextra -Wall src/*.c src/data_parsing/*.c src/planning/*.c src/utilities/*.c -o sql.exe`

## Run a query against a .db file
`sql.exe companies.db "SELECT id, name FROM companies WHERE country = 'chad'"`

## Architecture

- **Recursive descent parser** — produces an AST allocated in a single arena. The arena makes cleanup after a query trivial: one free for the entire parse tree.
- **Volcano iterator model** — each operator (scan, filter, project, aggregate) exposes a `next()` interface. The top of the tree pulls rows from the bottom one at a time, keeping memory usage flat regardless of table size.
- **B-tree storage** — data lives in a `.db` file paged in the SQLite format. The engine reads pages on demand with a fixed size cache and supports both table scans and index lookups.
- **Custom allocators and containers** — the arena allocator, dynamic array, and hash map are all hand-rolled. No standard library containers.

## What's next

- Full `WHERE` expression evaluation — the expression parser is complete; the evaluator is in progress
- `JOIN` support
- A basic query planner to choose between scan strategies based on cost

## What I learned

Why databases use fixed-size pages (random access without a directory), how the volcano model avoids loading entire result sets into memory, and why arena allocation is such a natural fit for tree-shaped data with a single lifetime.