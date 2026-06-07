#!/usr/bin/env sh
set -eu

cat <<'EOF'
VirtualBox setup for LiteNix OS:

1. Build the ISO:
   make iso

2. Create a VM:
   Type: Other/Unknown 64-bit
   RAM: 64 MB to 512 MB
   CPU: 1
   Storage: attach build/litenix.iso as optical media
   Audio: disabled
   USB: disabled initially

3. Boot the VM and verify the LiteNix banner appears.
EOF
