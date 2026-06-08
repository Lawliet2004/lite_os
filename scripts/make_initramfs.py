import sys
import os
import tarfile

def main():
    if len(sys.argv) < 3:
        print("Usage: make_initramfs.py <source_dir> <output_tar>")
        sys.exit(1)

    src_dir = sys.argv[1]
    out_tar = sys.argv[2]

    with tarfile.open(out_tar, "w", format=tarfile.USTAR_FORMAT) as tar:
        for root, dirs, files in os.walk(src_dir):
            # Sort to ensure deterministic tar order
            dirs.sort()
            files.sort()
            for name in dirs + files:
                full_path = os.path.join(root, name)
                arc_name = os.path.relpath(full_path, src_dir)
                arc_name = arc_name.replace(os.sep, '/')
                tar.add(full_path, arcname=arc_name)

if __name__ == "__main__":
    main()
