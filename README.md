
# Simple Filesystem in C

This project is a simple block-based, inode-style filesystem written in C for my Operating Systems course (Spring 2025). It runs on top of a simulated software disk provided via `softwaredisk.c`.

---

## 👨‍💻 Authors

- **Carter Mauer**  
- **Cole Heausler**  

---

## 🗂️ Project Structure

```
simple-filesystem-c/
├── src/
│   ├── filesystem.c
│   ├── formatfs.c
│   ├── softwaredisk.c
│   ├── filesystem.h
│   ├── softwaredisk.h
├── tests/
│   ├── testfs0.c
│   ├── testfs4a.c
│   └── exercisesoftwaredisk.c
├── README.md
├── filesystem_design.pdf
├── assignment-instructions.pdf
└── .gitignore

```

---

## 💾 Disk Layout

| Block Range | Purpose                                          |
|-------------|--------------------------------------------------|
| `0`         | Data Block Bitmap                                |
| `1`         | Inode Bitmap                                     |
| `2–5`       | Inodes (4 blocks, 128 per block = 512 total)     |
| `6–69`      | Directory Entries (64 blocks, 8 entries/block)   |
| `70–4095`   | File Data Blocks                                 |

---

## 📦 Filesystem Features

- Inode-based structure with 13 direct and 1 indirect pointer
- Flat directory layout (single-level, no subdirectories)
- 512 max files
- ~8 MB max file size per file
- Supports filenames up to 507 characters
- Bitmaps used for inode and block allocation
- Persistent storage through `softwaredisk` backend
- Error handling through `fserror` enum and `fs_print_error()`

---

## ⚙️ Building & Running

Navigate to the `src/` folder and build everything using `make`:

```bash
cd src/
make
```

### Format the Disk

```bash
./formatfs
```

### Run Tests

```bash
./testfs0     # Simple test
./testfs4a    # Alignment test
```

---

## 🔒 Error Handling

All file operations set the global variable `fserror`. You can check the result of any operation using:

```c
fs_print_error();
```

Common error types include:
- `FS_OUT_OF_SPACE`
- `FS_FILE_ALREADY_EXISTS`
- `FS_FILE_NOT_FOUND`
- `FS_IO_ERROR`

---

## 🛑 Limitations

- No support for subdirectories
- No permissions or timestamps
- Not thread-safe (single-process only)
- No crash recovery

---

## 📄 Design Document

See [`filesystem_design.md`](./filesystem_design.md) for a detailed breakdown of the physical layout, file structures, and implementation limits.

---

## 🧪 License

This project was developed as an educational assignment for CSC 4103. Not intended for production or commercial use.
