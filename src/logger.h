#pragma once

// Initialise the logger. If consoleMode is true, output is also printed to stdout.
// A log file (sonar_audio_switcher.log) is created next to the executable.
void logInit(bool consoleMode);

// Shut down the logger (flushes and closes the log file).
void logShutdown();

// Log a formatted message.  Goes to: OutputDebugString, log file, and (if
// console mode) stdout.
void logMsg(const char* fmt, ...);

