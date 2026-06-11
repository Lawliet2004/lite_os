# Desktop Roadmap

## Current Position

LiteNix does not yet have a native desktop stack. The next realistic target is a small compositor plus a shell/launcher layer, not a full GNOME/KDE clone.

## Milestone Order

1. Event loop and window protocol
2. Software-rendered compositor
3. Terminal window
4. Dock and top bar
5. File manager
6. Settings app
7. Launcher and `.desktop` integration

## Constraints

- Software rendering first
- Shared-memory buffers for window content
- AF_UNIX IPC for GUI clients
- Keep redraws dirty-rectangle based

## Current Gap

- No compositor
- No GUI window protocol
- No launcher/menu database
- No icon theme loader
