# Security Policy

## Windows Input and Game Compatibility

The Windows input backend subscribes to keyboard and mouse Raw Input using
`RIDEV_INPUTSINK | RIDEV_DEVNOTIFY` on a message-only window owned by BongoCat.
It preserves legacy input delivery and does not install global input hooks,
inject code into games, read or write game memory, simulate game input, or
install an input driver. The normal Windows runtime requests `asInvoker`
with `uiAccess="false"`; it does not request administrator privileges.

Foreground window and cursor queries provide read-only state for pointer
tracking and local diagnostics. Normal input diagnostics contain counters,
timing, and window/cursor metadata, rather than typed text. Development tools
that simulate input are separate `EXCLUDE_FROM_ALL` targets and are not part
of the application target or its installation rules.

Some game and system combinations still stop delivering background input
while the game has focus. This remains an unresolved compatibility issue;
version 1.6.0 does not claim to fix it for every game. Registration success
alone does not establish that input messages are arriving. BongoCat does
not attempt to bypass a game's input restrictions.

Using documented Windows APIs does not guarantee acceptance by every
anti-cheat product. The source checks in
`cmake/CheckPlatformRuntimeSafety.cmake` enforce the project's input API
boundaries; they are not an anti-cheat certification or a substitute for
testing the actual release build.

## Reporting Security Issues

We always aim to ship secure software and take security flaws seriously. Thank you for responsibly disclosing your findings to the team to keep the open source community a safe space.

To report a security issue, reach us at vladelaina@gmail.com. We will be in touch should we need any additional information or guidance to fix the bug.

