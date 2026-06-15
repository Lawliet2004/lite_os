#!/bin/sh

echo "----------------------------------------"
echo "  LiteNix System Initialization (rcS)"
echo "----------------------------------------"

# 1. Mount pseudo-filesystems (already done by init, but for safety)
# mount -t proc proc /proc
# mount -t sysfs sys /sys
# mount -t tmpfs tmp /tmp

# 2. Set hostname (either /etc/hostname or DHCP later will set /proc/sys/kernel/hostname)
if [ -f "/etc/hostname" ]; then
    hostname $(cat /etc/hostname)
fi

# 3. Start the kernel log daemon early so messages from the rest of the
#    boot (services, etc.) are persisted to /var/log/kern.log.
/sbin/klogd --daemon
echo "klogd started"

# 4. Configure networking — try DHCP first, fall back to static if no server
echo "Attempting DHCP on eth0..."
/sbin/dhcpcd 2>/dev/null
if [ $? -ne 0 ]; then
    echo "DHCP failed, using static configuration..."
    /sbin/ifconfig eth0 10.0.2.15 netmask 255.255.255.0 gw 10.0.2.2
fi

# 5. Start every service in /etc/services.enabled in dependency order
#    (AFTER="..." in each .conf). The new /sbin/svc understands
#    service definitions under /etc/services.available.
echo "Starting enabled services..."
/sbin/svc start-enabled

# 6. Start the respawn supervisor so crashed services come back.
echo "Starting service supervisor..."
/sbin/supervisor --daemon
echo "supervisor started"

# 7. Record a marker so external log analyzers know boot finished
/bin/logger "LiteNix boot complete"

echo "System initialization complete!"
echo "----------------------------------------"
