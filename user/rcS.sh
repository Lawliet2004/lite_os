#!/bin/sh

echo "----------------------------------------"
echo "  LiteNix System Initialization (rcS)"
echo "----------------------------------------"

# 1. Mount pseudo-filesystems (already done by init, but for safety)
# mount -t proc proc /proc
# mount -t sysfs sys /sys
# mount -t tmpfs tmp /tmp

# 2. Configure networking
if [ -f "/etc/hostname" ]; then
    hostname $(cat /etc/hostname)
fi

echo "Configuring static IP for eth0..."
/sbin/ifconfig eth0 10.0.2.15 netmask 255.255.255.0 gw 10.0.2.2 2>/dev/null

# 3. Launch enabled background services
ENABLED_FILE="/etc/services.enabled"
if [ -f "$ENABLED_FILE" ]; then
    echo "Launching enabled services..."
    while read -r svc; do
        if [ -n "$svc" ]; then
            /sbin/service "$svc" start
        fi
    done < "$ENABLED_FILE"
else
    # Fallback to defaults if no enabled file
    echo "Launching default services..."
    /sbin/service udp_echo start
    /sbin/service http_server start
fi

echo "System initialization complete!"
echo "----------------------------------------"
