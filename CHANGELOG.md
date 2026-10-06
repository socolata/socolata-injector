# Changelog

All notable changes to Socolata Injector will be documented in this file.

## [1.0.1] - 2026-10-06

### Added
- Version display in window title and status bar
- NtCreateThreadEx support for Native injection (more stealth)
- Fallback to CreateRemoteThread if NtCreateThreadEx fails

### Improved
- Native injection stability and cleanup
- Memory protection handling after WriteProcessMemory

### Fixed
- Minor GUI and logging improvements

---

## [1.0.0] - Initial Release

### Features
- Native Inject (LoadLibrary)
- Manual Map (x64 + x86)
- SetWindowsHook
- Valorant Hook
- JAR Injection
- Process icon + architecture detection
- Multiple file support
- Advanced Settings
- Live Log
- Clean modern GUI