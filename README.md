# DBZote
![Linux](https://img.shields.io/badge/Linux-E34F26?style=for-the-badge&logo=linux&logoColor=black)
![C](https://img.shields.io/badge/c-%2300599C.svg?style=for-the-badge&logo=c&logoColor=white)

**DBZote** is an experimental relational SQL database built **from scratch in C**, inspired by the architecture of [SQLite](https://www.sqlite.org/).  
The project aims to provide a minimal, single–file implementation that can be easily compiled and explored.

## 📜 Overview
- **Single-file design:** The entire database engine is implemented in a single C source file for simplicity and portability.  
- **SQLite-inspired architecture:** While not a direct copy, DBZote takes cues from SQLite’s modular yet lightweight design.  
- **Work in progress:** This is an ongoing learning project focused on understanding how databases work at a low level.

## 🚧 Current Status
- **Append-only storage:** Currently, DBZote functions as an **append-only database** with a predefined schema.  
- **Limited SQL support:** Only a basic structure is in place; SQL parsing and execution are minimal.  

The next major milestone is the **implementation of B-Trees** for indexing and efficient data retrieval.

## 🛠️ Goals
- Build a deeper understanding of database internals such as storage engines, indexing, and query execution.  
- Gradually evolve from a simple append-only store into a small, functional relational database engine.

## 🤝 Contributing

- This is primarily a learning project, but suggestions, feedback, and discussions are welcome.
- Feel free to open an issue or submit a pull request.
