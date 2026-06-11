#!/bin/sh

# LiteNix Service Manager v1

SVC_DIR="/etc/init.d"
ENABLED_FILE="/etc/services.enabled"

usage() {
    echo "LiteNix Service Manager"
    echo "Usage: service <command> [args]"
    echo ""
    echo "Commands:"
    echo "  list              List all services and their status"
    echo "  start <name>      Start a service"
    echo "  stop <name>       Stop a service"
    echo "  restart <name>    Restart a service"
    echo "  status <name>     Show service status"
    echo "  enable <name>     Enable service at boot"
    echo "  disable <name>    Disable service at boot"
    echo "  help              Show this help"
    exit 1
}

if [ $# -lt 1 ]; then
    usage
fi

CMD=$1
SVC=$2

is_enabled() {
    if [ -f "$ENABLED_FILE" ]; then
        grep -q "^$1$" "$ENABLED_FILE"
        return $?
    fi
    return 1
}

case "$CMD" in
    list)
        echo "Service          Status        Boot"
        echo "----------------------------------------"
        for s in "$SVC_DIR"/*; do
            if [ ! -f "$s" ]; then continue; fi
            name=$(basename "$s")
            if [ "$name" = "rcS" ]; then continue; fi
            
            boot="disabled"
            is_enabled "$name" && boot="enabled "
            
            status="stopped "
            # Simple check: is there a pid file? (Not all services use them yet)
            if [ -f "/run/${name}.pid" ]; then
                status="running "
            fi
            
            echo "$name             $status      $boot"
        done
        ;;

    start|stop|restart|status)
        if [ -z "$SVC" ]; then
            echo "Error: Service name required"
            exit 1
        fi
        script="$SVC_DIR/$SVC"
        if [ ! -f "$script" ]; then
            echo "Error: Service '$SVC' not found"
            exit 1
        fi
        sh "$script" "$CMD"
        ;;

    enable)
        if [ -z "$SVC" ]; then
            echo "Error: Service name required"
            exit 1
        fi
        if [ ! -f "$SVC_DIR/$SVC" ]; then
            echo "Error: Service '$SVC' not found"
            exit 1
        fi
        is_enabled "$SVC" || echo "$SVC" >> "$ENABLED_FILE"
        echo "Service '$SVC' enabled at boot."
        ;;

    disable)
        if [ -z "$SVC" ]; then
            echo "Error: Service name required"
            exit 1
        fi
        if [ -f "$ENABLED_FILE" ]; then
            # Simple sed-like removal without sed being robust
            grep -v "^$SVC$" "$ENABLED_FILE" > "${ENABLED_FILE}.tmp"
            mv "${ENABLED_FILE}.tmp" "$ENABLED_FILE"
        fi
        echo "Service '$SVC' disabled at boot."
        ;;

    help)
        usage
        ;;

    *)
        echo "Unknown command: $CMD"
        usage
        ;;
esac
