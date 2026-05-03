// TOMBSTONE — this file is intentionally excluded from CMakeLists.txt.
//
// It was an early monolithic prototype that duplicated the entry point and
// referenced APIs that no longer exist.  The authoritative entry point is:
//
//   src/main.cpp  →  enjoystick::app::Application::Create() → Init() → Run()
//
// Do NOT add this file back to any CMakeLists.txt target.
// It will be removed entirely in a future cleanup pass once git history
// no longer needs it as a reference.
