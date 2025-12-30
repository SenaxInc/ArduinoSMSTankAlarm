/**
 * TankAlarm_Platform.h
 * 
 * Platform detection and abstractions for TankAlarm 112025.
 * Handles differences between AVR, STM32, and Mbed OS platforms.
 * 
 * Copyright (c) 2025 Senax Inc. All rights reserved.
 */

#ifndef TANKALARM_PLATFORM_H
#define TANKALARM_PLATFORM_H

#include <Arduino.h>

// ============================================================================
// Platform Detection and PROGMEM Support
// ============================================================================

#if defined(ARDUINO_ARCH_AVR)
  #include <avr/pgmspace.h>
#else
  #ifndef PROGMEM
    #define PROGMEM
  #endif
  #ifndef pgm_read_byte_near
    #define pgm_read_byte_near(addr) (*(const uint8_t *)(addr))
  #endif
#endif

// ============================================================================
// Filesystem Support Detection
// ============================================================================
#if defined(ARDUINO_ARCH_STM32) && !defined(ARDUINO_ARCH_MBED)
  #include <LittleFS.h>
  #include <IWatchdog.h>
  #define TANKALARM_FILESYSTEM_AVAILABLE
  #define TANKALARM_WATCHDOG_AVAILABLE
#elif defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  #include <LittleFileSystem.h>
  #include <BlockDevice.h>
  #include <mbed.h>
  // Bring specific mbed types into global namespace
  using mbed::LittleFileSystem;
  using mbed::BlockDevice;
  using mbed::Watchdog;
  // Bring arduino types into global namespace for library compatibility
  using arduino::Stream;
  using arduino::Print;
  #define TANKALARM_FILESYSTEM_AVAILABLE
  #define TANKALARM_WATCHDOG_AVAILABLE
  #define TANKALARM_POSIX_FILE_IO_AVAILABLE
  #define TANKALARM_POSIX_FS_PREFIX "/fs"
#endif

// ============================================================================
// Watchdog Helper Class (Mbed OS)
// ============================================================================
#if defined(TANKALARM_WATCHDOG_AVAILABLE) && (defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED))
class MbedWatchdogHelper {
public:
  bool start(uint32_t timeoutMs) {
    mbed::Watchdog &watchdog = mbed::Watchdog::get_instance();
    return watchdog.start(timeoutMs);
  }
  
  void kick() {
    mbed::Watchdog &watchdog = mbed::Watchdog::get_instance();
    watchdog.kick();
  }
  
  uint32_t get_timeout() const {
    mbed::Watchdog &watchdog = mbed::Watchdog::get_instance();
    return watchdog.get_timeout();
  }
};
#endif

// ============================================================================
// Watchdog Macros (platform-agnostic)
// ============================================================================
#ifdef TANKALARM_WATCHDOG_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    #define TANKALARM_WATCHDOG_KICK(watchdog) (watchdog).kick()
    #define TANKALARM_WATCHDOG_START(watchdog, ms) (watchdog).start(ms)
  #else
    #define TANKALARM_WATCHDOG_KICK(watchdog) IWatchdog.reload()
    #define TANKALARM_WATCHDOG_START(watchdog, ms) IWatchdog.begin((ms) * 1000UL)
  #endif
#else
  #define TANKALARM_WATCHDOG_KICK(watchdog) ((void)0)
  #define TANKALARM_WATCHDOG_START(watchdog, ms) ((void)0)
#endif

// ============================================================================
// POSIX File I/O Helpers
// ============================================================================
#ifdef TANKALARM_POSIX_FILE_IO_AVAILABLE

#include <cstdio>
#include <cerrno>

/**
 * Get file size using POSIX file operations
 * @param fp Open file pointer
 * @return File size in bytes, or -1 on error
 */
static inline long tankalarm_posix_file_size(FILE *fp) {
  if (!fp) return -1;
  long currentPos = ftell(fp);
  if (currentPos < 0) return -1;
  if (fseek(fp, 0, SEEK_END) != 0) return -1;
  long size = ftell(fp);
  fseek(fp, currentPos, SEEK_SET);
  return size;
}

/**
 * Check if file exists using POSIX file operations
 * @param path File path
 * @return true if file exists
 */
static inline bool tankalarm_posix_file_exists(const char *path) {
  FILE *fp = fopen(path, "r");
  if (fp) {
    fclose(fp);
    return true;
  }
  return false;
}

/**
 * Log POSIX error for debugging
 * @param operation Operation name
 * @param path File path
 */
static inline void tankalarm_posix_log_error(const char *operation, const char *path) {
#ifdef DEBUG_MODE
  Serial.print(F("POSIX error in "));
  Serial.print(operation);
  Serial.print(F(" for "));
  Serial.print(path);
  Serial.print(F(": errno="));
  Serial.println(errno);
#else
  (void)operation;
  (void)path;
#endif
}

#endif // TANKALARM_POSIX_FILE_IO_AVAILABLE

#endif // TANKALARM_PLATFORM_H
