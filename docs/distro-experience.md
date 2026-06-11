# LiteNix Distro Experience

LiteNix provides a small distro-like CLI experience on a custom Linux-compatible kernel. It is designed to feel familiar to users of Ubuntu or Arch, while running on an entirely independent kernel implementation.

## Features

- **Live ISO Environment**: Boot into a functional live environment with a full directory skeleton.
- **Boot Banner & MOTD**: A clean, informative welcome message on boot.
- **Installer**: A real first-stage installer (`litenix-install`) to transfer the system to persistent storage.
- **User Accounts**: Basic support for root account and `/etc/passwd`.
- **Package Manager**: `lpkg` (LiteNix Package Manager) for installing, removing, and verifying software.
- **Service Manager**: `service` command to manage background daemons.
- **Networking**: Tools like `ifconfig`, `dnslookup`, and `http_client` for interacting with the network.

## Quick Start

### Building the ISO
```bash
make
```

### Running in QEMU (Live Mode)
```bash
make run
```

### Installing to Persistent Disk
From the live shell:
```bash
litenix-install --target /persist
```
After installation, the system files will persist on the `disk.img` (mounted at `/persist`).

## Using the Package Manager (lpkg)

### List available packages
```bash
lpkg list
```

### Install a package
```bash
lpkg install hello
```

### List installed packages
```bash
lpkg installed
```

### Verify a package
```bash
lpkg verify hello
```

## Managing Services

### List services
```bash
service list
```

### Start/Stop a service
```bash
service start httpd
service stop httpd
```

### Enable a service at boot
```bash
service enable httpd
```

## Known Limitations

- **No full Ubuntu/Arch compatibility**: LiteNix is NOT compatible with Ubuntu/Arch packages. It uses its own `.lpkg` format.
- **No Multi-user Security**: While `/etc/passwd` exists, the kernel does not yet fully enforce multi-user permission boundaries.
- **Static IP**: DHCP is not yet implemented.
- **Manual Persistence**: In v1, you must manually mount or install to `/persist` for changes to survive reboots.

## Future Milestones

- DHCP support
- Real password hashing
- Repository sync over HTTP
- Basic framebuffer-based GUI
