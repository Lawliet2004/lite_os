import os
import stat
import sys
import tarfile


# Mode overrides applied at tar-pack time. The kernel's initramfs unpacker
# reads mode bits from the tar header (parse_octal in initramfs.c), so this is
# the canonical place to mark privileged binaries setuid and to lock down
# sensitive files. Host file modes (esp. on Windows NTFS) cannot represent
# setuid, so we have to override here.
MODE_OVERRIDES = {
    # Privileged tools that need to switch users
    "bin/login":  0o4755,
    "bin/passwd": 0o4755,
    "bin/su":     0o4755,

    # Sensitive system files (root-only)
    "etc/shadow": 0o0600,

    # Test fixtures for Phase 9 setuid-exec verification
    "tests/show_creds": 0o4755,
}


def main() -> int:
    if len(sys.argv) < 3:
        print("Usage: make_initramfs.py <source_dir> <output_tar>")
        return 1

    src_dir = sys.argv[1]
    out_tar = sys.argv[2]
    overrides_applied: list[str] = []

    with tarfile.open(out_tar, "w", format=tarfile.USTAR_FORMAT) as tar:
        for root, dirs, files in os.walk(src_dir):
            # Sort to ensure deterministic tar order
            dirs.sort()
            files.sort()
            for name in dirs + files:
                full_path = os.path.join(root, name)
                arc_name = os.path.relpath(full_path, src_dir).replace(os.sep, "/")

                tarinfo = tar.gettarinfo(full_path, arcname=arc_name)
                if tarinfo is None:
                    continue

                # Force ownership to root:root inside the image — the host's
                # uid/gid is irrelevant.
                tarinfo.uid = 0
                tarinfo.gid = 0
                tarinfo.uname = "root"
                tarinfo.gname = "root"

                if arc_name in MODE_OVERRIDES:
                    new_mode = MODE_OVERRIDES[arc_name]
                    if tarinfo.mode != new_mode:
                        overrides_applied.append(f"{arc_name}={oct(new_mode)}")
                    tarinfo.mode = new_mode

                if tarinfo.isreg():
                    with open(full_path, "rb") as f:
                        tar.addfile(tarinfo, f)
                else:
                    tar.addfile(tarinfo)

    if overrides_applied:
        print(f"make_initramfs: mode overrides applied: {', '.join(overrides_applied)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
