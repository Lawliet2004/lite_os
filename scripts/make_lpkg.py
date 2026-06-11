import os
import sys
import struct

def calculate_checksum(data):
    # Simple additive checksum
    return sum(data) & 0xffffffff

def main():
    if len(sys.argv) < 3:
        print("Usage: make_lpkg.py <source_dir> <output.lpkg>")
        sys.exit(1)

    src_dir = sys.argv[1]
    out_path = sys.argv[2]

    files_to_pack = []
    for root, _, files in os.walk(src_dir):
        for f in files:
            full_path = os.path.join(root, f)
            rel_path = os.path.relpath(full_path, src_dir).replace('\\', '/')
            files_to_pack.append((full_path, rel_path))

    print(f"Packing {len(files_to_pack)} files into {out_path}...")

    with open(out_path, 'wb') as out_f:
        # Magic: LPKG
        out_f.write(b'LPKG')
        # File count
        out_f.write(struct.pack('<I', len(files_to_pack)))

        for full, rel in files_to_pack:
            stat = os.stat(full)
            mode = stat.st_mode
            with open(full, 'rb') as f:
                data = f.read()

            checksum = calculate_checksum(data)
            rel_bytes = rel.encode('utf-8')

            # Write file entry
            out_f.write(struct.pack('<I', len(rel_bytes)))
            out_f.write(rel_bytes)
            out_f.write(struct.pack('<I', mode))
            out_f.write(struct.pack('<I', len(data)))
            out_f.write(struct.pack('<I', checksum))
            out_f.write(data)

    print("Done.")

if __name__ == '__main__':
    main()
