# POSIX API Compliance Documentation
**Date:** December 25, 2025  
**Platform:** Arduino Opta (STM32H747XI with Mbed OS)  
**Codebase:** TankAlarm 112025 (Client & Server)  
**Status:** ✅ POSIX Compliant

---

## Overview

The TankAlarm 112025 codebase has been updated to be POSIX API compliant for file operations. This ensures maximum portability and compatibility with the Mbed OS VFS (Virtual File System) layer on the Arduino Opta platform.

---

## POSIX Compliance Summary

### Standard Headers Included

The following POSIX-compliant headers are now included in both Client and Server:

```cpp
// POSIX-compliant standard library headers
#include <stdio.h>    // fopen, fread, fwrite, fclose, fseek, ftell, fprintf, etc.
#include <stdlib.h>   // malloc, free, exit
#include <errno.h>    // errno, error codes
#include <sys/types.h> // ssize_t, size_t, off_t

// POSIX file I/O types (Mbed OS platforms)
#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  #include <fcntl.h>     // O_RDONLY, O_WRONLY, O_CREAT, etc.
  #include <sys/stat.h>  // struct stat, file mode constants
#endif
```

### POSIX File Operations Used

| POSIX Function | Description | Used In |
|----------------|-------------|---------|
| `fopen()` | Open file stream | Config read/write, note buffering |
| `fclose()` | Close file stream | All file operations |
| `fread()` | Read from file stream | Config loading, buffer processing |
| `fwrite()` | Write to file stream | Config saving, log writing |
| `fseek()` | Seek within file | File size calculation, position |
| `ftell()` | Get current position | File size calculation |
| `fgets()` | Read line from file | Buffer processing |
| `fgetc()` | Read character | Line parsing |
| `fprintf()` | Formatted file output | Note buffering |
| `ferror()` | Check for stream errors | Error handling |
| `remove()` | Delete file | Buffer cleanup, temp files |
| `rename()` | Rename/move file | Atomic file updates |

### POSIX Types Used

| Type | Description | Usage |
|------|-------------|-------|
| `FILE *` | File stream pointer | All file operations |
| `size_t` | Unsigned size type | Buffer sizes, byte counts |
| `ssize_t` | Signed size type | Return values (can be -1 for errors) |
| `long` | File position/size | ftell() return type |

---

## Platform-Specific Implementation

### Mbed OS (Arduino Opta) - POSIX Native

Mbed OS provides full POSIX compatibility through its Virtual File System (VFS) layer:

```cpp
#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  #define POSIX_FILE_IO_AVAILABLE  // Mbed OS supports POSIX file operations
  #define POSIX_FS_PREFIX "/fs"    // VFS mount point
#endif
```

**File Path Convention:**
- All files are accessed via `/fs/` prefix (e.g., `/fs/client_config.json`)
- This maps to the LittleFileSystem mounted at the "fs" mount point

**Example POSIX File Read:**
```cpp
FILE *file = fopen("/fs/client_config.json", "r");
if (file) {
  fseek(file, 0, SEEK_END);
  long fileSize = ftell(file);
  fseek(file, 0, SEEK_SET);
  
  char *buffer = (char *)malloc(fileSize + 1);
  size_t bytesRead = fread(buffer, 1, fileSize, file);
  buffer[bytesRead] = '\0';
  fclose(file);
  
  // Process buffer...
  free(buffer);
}
```

**Example POSIX File Write:**
```cpp
FILE *file = fopen("/fs/client_config.json", "w");
if (file) {
  const char *data = "{\"key\":\"value\"}";
  size_t written = fwrite(data, 1, strlen(data), file);
  if (ferror(file) || written != strlen(data)) {
    // Handle error - check errno
  }
  fclose(file);
}
```

### STM32duino (Non-Mbed) - Arduino API

For non-Mbed STM32 platforms, the Arduino LittleFS API is used instead:

```cpp
#if defined(ARDUINO_ARCH_STM32) && !defined(ARDUINO_ARCH_MBED)
  // Uses Arduino File class
  File file = LittleFS.open("/client_config.json", "r");
  // ... Arduino-style operations
  file.close();
#endif
```

---

## POSIX Helper Functions

The codebase includes several POSIX-compliant helper functions:

### `posix_file_size(FILE *fp)`
Returns the size of an open file without changing the file position.

```cpp
static long posix_file_size(FILE *fp) {
  if (!fp) return -1;
  long currentPos = ftell(fp);
  if (currentPos < 0) return -1;
  if (fseek(fp, 0, SEEK_END) != 0) return -1;
  long size = ftell(fp);
  fseek(fp, currentPos, SEEK_SET);  // Restore position
  return size;
}
```

### `posix_file_exists(const char *path)`
Checks if a file exists using POSIX `fopen()`.

```cpp
static bool posix_file_exists(const char *path) {
  FILE *fp = fopen(path, "r");
  if (fp) {
    fclose(fp);
    return true;
  }
  return false;
}
```

### `posix_log_error(const char *operation, const char *path)`
Logs POSIX errors with errno information (DEBUG_MODE only).

```cpp
static void posix_log_error(const char *operation, const char *path) {
  #ifdef DEBUG_MODE
  Serial.print(F("POSIX error in "));
  Serial.print(operation);
  Serial.print(F(" for "));
  Serial.print(path);
  Serial.print(F(": errno="));
  Serial.println(errno);
  #endif
}
```

### `posix_write_file(const char *path, const char *data, size_t len)` (Server only)
Safe file write with comprehensive error handling.

### `posix_read_file(const char *path, char *buffer, size_t bufSize)` (Server only)
Safe file read with comprehensive error handling.

---

## Error Handling

POSIX error handling is implemented using the standard `errno` mechanism:

1. **Check Return Values**: All POSIX functions return values indicate success/failure
2. **Set errno**: Functions set `errno` on failure
3. **Log Errors**: `posix_log_error()` reports errno in debug mode

**Common errno Values:**
| errno | Constant | Description |
|-------|----------|-------------|
| 2 | ENOENT | File not found |
| 5 | EIO | I/O error |
| 12 | ENOMEM | Out of memory |
| 13 | EACCES | Permission denied |
| 17 | EEXIST | File exists |
| 21 | EISDIR | Is a directory |
| 28 | ENOSPC | No space left on device |

---

## File Operations Summary

### Configuration Files

| File | Operation | POSIX Function |
|------|-----------|----------------|
| `/fs/client_config.json` | Read | `fopen()`, `fread()`, `fclose()` |
| `/fs/client_config.json` | Write | `fopen()`, `fwrite()`, `fclose()` |
| `/fs/server_config.json` | Read | `fopen()`, `fread()`, `fclose()` |
| `/fs/server_config.json` | Write | `fopen()`, `fwrite()`, `fclose()` |

### Buffer/Log Files

| File | Operation | POSIX Function |
|------|-----------|----------------|
| `/fs/pending_notes.log` | Append | `fopen("a")`, `fprintf()`, `fclose()` |
| `/fs/pending_notes.log` | Read Lines | `fopen("r")`, `fgets()`, `fclose()` |
| `/fs/pending_notes.tmp` | Write | `fopen("w")`, `fwrite()`, `fclose()` |
| `/fs/pending_notes.log` | Delete | `remove()` |
| `/fs/pending_notes.tmp` | Rename | `rename()` |

### Cache/Data Files (Server)

| File | Operation | POSIX Function |
|------|-----------|----------------|
| `/fs/client_config_cache.txt` | Read/Write | `fopen()`, `fread()`/`fwrite()`, `fclose()` |
| `/fs/calibration_log.txt` | Append | `fopen("a")`, `fprintf()`, `fclose()` |
| `/fs/calibration_data.txt` | Read/Write | `fopen()`, `fgets()`/`fprintf()`, `fclose()` |
| `/fs/history_settings.json` | Read/Write | `fopen()`, `fread()`/`fwrite()`, `fclose()` |

---

## Atomic File Updates

For critical data like configuration files, atomic updates are performed using the POSIX rename pattern:

```cpp
// 1. Write to temporary file
FILE *tmp = fopen("/fs/config.tmp", "w");
fwrite(data, 1, len, tmp);
fclose(tmp);

// 2. Remove old file
remove("/fs/config.json");

// 3. Rename temp to final (atomic on most filesystems)
rename("/fs/config.tmp", "/fs/config.json");
```

This pattern ensures that power loss during write operations won't corrupt the configuration file.

---

## Thread Safety

On Mbed OS, file operations are thread-safe when using POSIX file streams:

- `FILE *` streams are protected by internal mutexes
- `errno` is thread-local in Mbed OS
- Multiple threads can safely access different files simultaneously

**Note:** Concurrent access to the same file from multiple threads requires application-level synchronization.

---

## Testing Recommendations

### POSIX Compliance Tests

1. **File Create/Read/Write**: Verify basic POSIX operations work correctly
2. **Error Handling**: Test file operations on non-existent files, verify errno
3. **Atomic Updates**: Verify rename-based atomic updates work correctly
4. **Buffer Overflow**: Test with files larger than available buffers
5. **Path Handling**: Verify `/fs/` prefix is handled correctly

### Platform Compatibility Tests

1. **Mbed OS (Opta)**: Test all file operations on Arduino Opta
2. **STM32duino**: Verify Arduino API fallback works on non-Mbed platforms
3. **Cross-Platform**: Same configuration should work across platforms

---

## Migration Notes

If updating from pre-POSIX code:

1. **Headers**: Ensure `<stdio.h>`, `<stdlib.h>`, `<errno.h>` are included
2. **File Paths**: Use `/fs/` prefix for all file paths on Mbed OS
3. **Error Checking**: Check return values and errno after file operations
4. **Resource Cleanup**: Always close files with `fclose()`, free malloc'd buffers
5. **Type Safety**: Use `size_t` for sizes, `ssize_t` for signed returns

---

## References

- [POSIX.1-2017 Standard](https://pubs.opengroup.org/onlinepubs/9699919799/)
- [Mbed OS File System Documentation](https://os.mbed.com/docs/mbed-os/latest/apis/file-system.html)
- [Mbed OS POSIX API Support](https://os.mbed.com/docs/mbed-os/latest/apis/posix-api.html)
- [LittleFS Design Documentation](https://github.com/littlefs-project/littlefs/blob/master/DESIGN.md)
