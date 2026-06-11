#!/bin/sh

DB_DIR="/var/lib/pkg/installed"

mkdir -p "$DB_DIR"

usage() {
    echo "LiteNix Package Manager"
    echo "Usage: pkg [install|remove|list|info|upgrade] [arguments]"
    exit 1
}

if [ $# -lt 1 ]; then
    usage
fi

CMD=$1
shift

case "$CMD" in
    install)
        if [ $# -lt 1 ]; then
            echo "Usage: pkg install <package.tar>"
            exit 1
        fi
        PKG_PATH=$1
        if [ ! -f "$PKG_PATH" ]; then
            echo "Error: package file '$PKG_PATH' not found"
            exit 1
        fi

        # Extract .PKGINFO
        rm -f /tmp/.PKGINFO
        tar -xf "$PKG_PATH" .PKGINFO -C /tmp/ 2>/dev/null
        if [ ! -f /tmp/.PKGINFO ]; then
            echo "Error: package is missing .PKGINFO"
            exit 1
        fi

        NAME=$(grep "^name=" /tmp/.PKGINFO | cut -d'=' -f2-)
        VERSION=$(grep "^version=" /tmp/.PKGINFO | cut -d'=' -f2-)
        DEPS=$(grep "^deps=" /tmp/.PKGINFO | cut -d'=' -f2-)

        if [ -z "$NAME" ] || [ -z "$VERSION" ]; then
            echo "Error: invalid .PKGINFO (missing name or version)"
            exit 1
        fi

        # Check dependencies
        if [ -n "$DEPS" ]; then
            OLD_IFS=$IFS
            IFS=','
            for dep in $DEPS; do
                dep=$(echo "$dep" | tr -d ' ')
                if [ -n "$dep" ] && [ ! -f "$DB_DIR/${dep}.meta" ]; then
                    echo "Warning: missing dependency '$dep'"
                fi
            done
            IFS=$OLD_IFS
        fi

        echo "Installing $NAME ($VERSION)..."

        # Save file list
        tar -tf "$PKG_PATH" > "$DB_DIR/${NAME}.list"

        # Extract to /
        tar -xf "$PKG_PATH" -C /

        # Copy metadata
        cp /tmp/.PKGINFO "$DB_DIR/${NAME}.meta"

        echo "Successfully installed $NAME-$VERSION"
        ;;

    remove)
        if [ $# -lt 1 ]; then
            echo "Usage: pkg remove <package_name>"
            exit 1
        fi
        NAME=$1

        if [ ! -f "$DB_DIR/${NAME}.list" ]; then
            echo "Error: package '$NAME' is not installed"
            exit 1
        fi

        echo "Removing $NAME..."

        # Read list and delete files
        while read -r file; do
            if [ -z "$file" ] || [ "$file" = ".PKGINFO" ] || [ "$file" = "./.PKGINFO" ]; then
                continue
            fi
            
            clean_file=$(echo "$file" | sed 's|^\./||')
            
            if [ -f "/$clean_file" ]; then
                rm -f "/$clean_file"
            elif [ -L "/$clean_file" ]; then
                rm -f "/$clean_file"
            fi
        done < "$DB_DIR/${NAME}.list"

        # Clean up lists
        rm -f "$DB_DIR/${NAME}.list"
        rm -f "$DB_DIR/${NAME}.meta"

        echo "Successfully removed $NAME"
        ;;

    list)
        echo "Installed packages:"
        for meta in "$DB_DIR"/*.meta; do
            if [ ! -f "$meta" ]; then
                echo "  None"
                break
            fi
            name=$(grep "^name=" "$meta" | cut -d'=' -f2-)
            ver=$(grep "^version=" "$meta" | cut -d'=' -f2-)
            desc=$(grep "^desc=" "$meta" | cut -d'=' -f2-)
            echo "  $name $ver - $desc"
        done
        ;;

    info)
        if [ $# -lt 1 ]; then
            echo "Usage: pkg info <package_name>"
            exit 1
        fi
        NAME=$1

        if [ ! -f "$DB_DIR/${NAME}.meta" ]; then
            echo "Error: package '$NAME' is not installed"
            exit 1
        fi

        echo "--- Package Metadata ---"
        cat "$DB_DIR/${NAME}.meta"
        echo ""
        echo "--- Package Files ---"
        cat "$DB_DIR/${NAME}.list"
        ;;

    upgrade)
        if [ $# -lt 1 ]; then
            echo "Usage: pkg upgrade <package.tar>"
            exit 1
        fi
        PKG_PATH=$1
        if [ ! -f "$PKG_PATH" ]; then
            echo "Error: package file '$PKG_PATH' not found"
            exit 1
        fi

        # Extract name from .PKGINFO
        rm -f /tmp/.PKGINFO
        tar -xf "$PKG_PATH" .PKGINFO -C /tmp/ 2>/dev/null
        if [ ! -f /tmp/.PKGINFO ]; then
            echo "Error: package is missing .PKGINFO"
            exit 1
        fi

        NAME=$(grep "^name=" /tmp/.PKGINFO | cut -d'=' -f2-)
        if [ -z "$NAME" ]; then
            echo "Error: invalid package metadata"
            exit 1
        fi

        if [ -f "$DB_DIR/${NAME}.list" ]; then
            echo "Upgrading $NAME..."
            sh "$0" remove "$NAME"
        else
            echo "Installing $NAME for the first time..."
        fi

        sh "$0" install "$PKG_PATH"
        ;;

    *)
        usage
        ;;
esac
