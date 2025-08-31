# StartupMonitor64

**StartupMonitor64** is a modern reimplementation of Mike Lin's original *StartupMonitor* (2004).  
It has been updated to support **64-bit Windows autostart locations** and handles **UAC prompts** through a helper executable (`SM64_Delete.exe`).  

---

## Features
- Monitors changes to Windows startup entries in real-time.
- Supports both **32-bit** and **64-bit** registry autostart locations.
- Provides a confirmation dialog before allowing or denying changes.
- Uses a separate helper (`SM64_Delete.exe`) to handle UAC elevation when deleting startup entries.

---

## Installation
1. Download or build the two executables:
   - `StartupMonitor64.exe`
   - `SM64_Delete.exe`
2. Place them together in a folder, for example:  
C:\Program Files\StartupMonitor64

3. Add `StartupMonitor64.exe` as an **autostart entry** in Windows so it runs automatically after reboot:
- Press `Win + R`, type `shell:startup` and press **Enter**.
- Copy a shortcut to `StartupMonitor64.exe` into this folder.

---

## Usage
- When a program attempts to add itself to startup, **StartupMonitor64** will show a confirmation dialog.  
- If deletion of an entry requires administrator privileges, the helper `SM64_Delete.exe` will be called automatically and handle UAC elevation.

---

## Notes
- Both executables must remain in the same folder for proper operation.
- This is **not the original software by Mike Lin**, but a community reimplementation adapted for modern Windows.

---

## Credits
- Original concept: [Mike Lin – StartupMonitor (2004)](http://www.mlin.net/StartupMonitor.shtml)  
- Updated and reimplemented for 64-bit Windows by the community.
