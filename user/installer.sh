#!/bin/sh

# LiteNix Installer v1

TARGET="/"
DRY_RUN=0

usage() {
    echo "LiteNix Installer"
    echo "Usage: litenix-install [options]"
    echo ""
    echo "Options:"
    echo "  --target <dir>    Target directory for installation (default: /)"
    echo "  --dry-run         Show what would be done without making changes"
    echo "  --help            Show this help"
    exit 1
}

while [ $# -gt 0 ]; do
    case "$1" in
        --target)
            TARGET="$2"
            shift 2
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        --help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

echo "LiteNix Installer"
echo "Target: $TARGET"
if [ "$DRY_RUN" = "1" ]; then
    echo "MODE: DRY RUN (no changes will be made)"
fi
echo ""

step() {
    echo "[$1/6] $2..."
}

# 1. Check target
step 1 "Checking target"
if [ ! -d "$TARGET" ]; then
    if [ "$DRY_RUN" = "0" ]; then
        mkdir -p "$TARGET"
    fi
fi
echo "OK"

# 2. Create directory layout
step 2 "Creating directory layout"
DIRS="bin sbin etc usr/bin usr/sbin usr/lib var/log var/lib/lpkg/installed var/cache/lpkg home/root tmp run dev proc sys"
for d in $DIRS; do
    if [ "$DRY_RUN" = "1" ]; then
        echo "  mkdir -p $TARGET/$d"
    else
        mkdir -p "$TARGET/$d"
    fi
done
echo "OK"

# 3. Install base system
step 3 "Installing base system"
# In a real distro, we would extract a base tarball.
# Here, we copy from the live environment (initramfs).
SOURCES="bin sbin etc usr"
for s in $SOURCES; do
    if [ "$DRY_RUN" = "1" ]; then
        echo "  cp -r /$s/* $TARGET/$s/"
    else
        cp -r "/$s/"* "$TARGET/$s/" 2>/dev/null
    fi
done
echo "OK"

# 4. Writing configuration
step 4 "Writing configuration"
if [ "$DRY_RUN" = "1" ]; then
    echo "  Write $TARGET/etc/fstab"
    echo "  Write $TARGET/etc/hostname"
else
    echo "/dev/hda /persist ext2 defaults 0 0" > "$TARGET/etc/fstab"
    echo "litenix-installed" > "$TARGET/etc/hostname"
fi
echo "OK"

# 5. Initializing package database
step 5 "Initializing package database"
if [ "$DRY_RUN" = "1" ]; then
    echo "  mkdir -p $TARGET/var/lib/lpkg/installed"
else
    mkdir -p "$TARGET/var/lib/lpkg/installed"
fi
echo "OK"

# 6. Installation complete
step 6 "Installation complete"
echo ""
echo "LiteNix has been successfully installed to $TARGET."
echo "Reboot with:"
echo "  power reboot"
echo ""

if [ "$DRY_RUN" = "1" ]; then
    echo "Dry run complete. No changes were made."
fi
