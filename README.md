# Mini Version Control System

A simplified implementation of a version control system similar to Git, supporting basic operations like initialization, adding files, committing changes, and viewing history.

## Prerequisites

- C++17 compatible compiler
- OpenSSL development libraries
- zlib development libraries
- Make

On Ubuntu, you can install the required libraries with:
```bash
sudo apt-get install build-essential libssl-dev zlib1g-dev
```

## Building the Project

1. Run `make` in the project directory
2. The executable `mygit` will be created

## Supported Commands

1. Initialize Repository:
```bash
./mygit init
```

2. Hash Object:
```bash
./mygit hash-object [-w] <file>
```

3. Cat File:
```bash
./mygit cat-file <flag> <sha>
```
Supported flags:
- `-p`: Print content
- `-t`: Show type
- `-s`: Show size

4. Write Tree:
```bash
./mygit write-tree
```

5. List Tree:
```bash
./mygit ls-tree [--name-only] <tree_sha>
```

6. Add Files:
```bash
./mygit add <file1> [file2 ...]
./mygit add .
```

7. Commit Changes:
```bash
./mygit commit -m "Commit message"
```

8. View Log:
```bash
./mygit log
```

9. Checkout:
```bash
./mygit checkout <commit_sha>
```

## Implementation Details

- Uses SHA-1 for content addressing
- Implements object compression using zlib
- Stores objects in a content-addressable filesystem
- Maintains an index for staging changes
- Supports basic branching through HEAD references

## Assumptions

1. Single branch (master) support only
2. No remote repository functionality
3. No merge capabilities
4. Simple commit message handling
5. Basic error handling for common cases
6. No support for symbolic links
7. No support for file modes/permissions

## File Structure

- `mygit.cpp`: Main implementation file
- `makefile`: Build configuration
- `README.md`: This documentation file

## Error Handling

The implementation includes basic error handling for:
- File not found
- Invalid SHA-1 hashes
- Invalid commands or arguments
- Repository initialization errors
- Object read/write errors

