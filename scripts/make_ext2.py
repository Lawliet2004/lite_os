import struct
import sys
import os

def make_ext2(filename, source_dir):
    # EXT2 constants
    EXT2_SUPER_MAGIC = 0xEF53
    BLOCK_SIZE = 1024
    INODE_SIZE = 128
    
    # Simple block/inode allocator
    # We reserve blocks 0-19 for metadata and inode table
    # Block 0: Boot record
    # Block 1: Superblock
    # Block 2: Group Descriptor
    # Block 3: Block Bitmap
    # Block 4: Inode Bitmap
    # Block 5-19: Inode Table (15 blocks * 8 inodes/block = 120 inodes)
    next_block = 20
    next_inode = 20 # Reserved inodes (1-19)
    
    def alloc_block():
        nonlocal next_block
        b = next_block
        next_block += 1
        return b
        
    def alloc_inode():
        nonlocal next_inode
        i = next_inode
        next_inode += 1
        return i

    files_to_add = []
    dirs_to_add = set()
    for root, dirs, files in os.walk(source_dir):
        for d in dirs:
            host_path = os.path.join(root, d)
            rel_path = os.path.relpath(host_path, source_dir)
            dirs_to_add.add(rel_path.replace('\\', '/'))
        for name in files:
            host_path = os.path.join(root, name)
            rel_path = os.path.relpath(host_path, source_dir)
            files_to_add.append((rel_path.replace('\\', '/'), host_path))
            
    print(f"DEBUG make_ext2 files_to_add={files_to_add}")
    print(f"DEBUG make_ext2 dirs_to_add={dirs_to_add}")

    with open(filename, 'r+b') as f:
        # 1. Superblock
        f.seek(1024)
        sb = struct.pack('<LLLLLLLLLLL LL HH H HH L',
            1024, 64*1024, 0, 64*1024-200, 1024-20, 1, 0, 0,
            8192, 8192, 128, 0, 0, 1, 20, EXT2_SUPER_MAGIC, 1, 1, 0
        )
        f.write(sb)
        
        # 2. Group Descriptor
        f.seek(2048)
        gd = struct.pack('<LLLHHH', 3, 4, 5, 100, 100, 1)
        f.write(gd)
        
        # Inode Table (Inode 2 is root, 11 is hello.txt)
        dir_entries = { "": [(2, 2, "."), (2, 2, ".."), (11, 1, "hello.txt")] }
        dir_blocks = { "": 7 } # Root dir uses block 7 (inside inode table range? Wait!)
        
        # Wait! If Block 5-19 is Inode Table, then Block 7 is INSIDE the table!
        # Root dir block should be outside. Let's use 20 for root dir.
        root_dir_blk = alloc_block()
        dir_blocks[""] = root_dir_blk

        # hello.txt (Inode 11) - Expected by init.c Test 16
        hello_blk = alloc_block()
        f.seek(5 * BLOCK_SIZE + (11 - 1) * INODE_SIZE)
        content = b"Hello from EXT2 disk!\n"
        in_hello = struct.pack('<HH LLLLL HH L L L 15L LLLL 12s',
            0x81A4, 0, len(content), 0, 0, 0, 0, 0, 1, 2, 0, 0,
            hello_blk, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, b'\x00' * 12
        )
        f.write(in_hello)
        f.seek(hello_blk * BLOCK_SIZE)
        f.write(content)

        def ensure_dir(dname):
            if not dname or dname in dir_entries:
                return
            parts = dname.split('/')
            parent = '/'.join(parts[:-1])
            ensure_dir(parent)
            
            dir_ino = alloc_inode()
            dir_blk = alloc_block()
            dir_entries[dname] = [(dir_ino, 2, "."), (2, 2, "..")]
            dir_entries[parent].append((dir_ino, 2, parts[-1]))
            dir_blocks[dname] = dir_blk
            
            f.seek(5 * BLOCK_SIZE + (dir_ino - 1) * INODE_SIZE)
            in_dir = struct.pack('<HH LLLLL HH L L L 15L LLLL 12s',
                0x41ED, 0, BLOCK_SIZE, 0, 0, 0, 0, 0, 2, 2, 0, 0,
                dir_blk, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, b'\x00' * 12
            )
            f.write(in_dir)

        for d in sorted(list(dirs_to_add)):
            ensure_dir(d)

        for rel_path, host_path in files_to_add:
            print(f"DEBUG make_ext2: rel_path='{rel_path}'")
            if rel_path == "hello.txt":
                continue # Already added manually as Inode 11
                
            parts = rel_path.split('/')
            if len(parts) > 1:
                dirname = '/'.join(parts[:-1])
                filename_part = parts[-1]
            else:
                dirname = ""
                filename_part = rel_path
                
            ensure_dir(dirname)
            
            # File
            ino = alloc_inode()
            with open(host_path, 'rb') as hf: data = hf.read()
            num_blks = (len(data) + BLOCK_SIZE - 1) // BLOCK_SIZE
            start_blk = alloc_block()
            for _ in range(num_blks - 1): alloc_block()
            
            f.seek(start_blk * BLOCK_SIZE)
            f.write(data)
            
            f.seek(5 * BLOCK_SIZE + (ino - 1) * INODE_SIZE)
            blks_arr = [0] * 15
            for j in range(min(num_blks, 12)): blks_arr[j] = start_blk + j
            in_file = struct.pack('<HH LLLLL HH L L L 15L LLLL 12s',
                0x81ED if "bin/" in rel_path else 0x81A4, 0, len(data), 0, 0, 0, 0, 0, 1, num_blks*2, 0, 0,
                *blks_arr, 0, 0, 0, 0, b'\x00' * 12
            )
            f.write(in_file)
            dir_entries[dirname].append((ino, 1, filename_part))

        # Write root inode last
        f.seek(5 * BLOCK_SIZE + (2 - 1) * INODE_SIZE)
        root_inode = struct.pack('<HH LLLLL HH L L L 15L LLLL 12s',
            0x41ED, 0, BLOCK_SIZE, 0, 0, 0, 0, 0, 2, 2, 0, 0,
            root_dir_blk, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, b'\x00' * 12
        )
        f.write(root_inode)

        # Write dir blocks
        for dirname, entries in dir_entries.items():
            print(f"DEBUG make_ext2 dir '{dirname}': {entries}")
            blk = dir_blocks[dirname]
            f.seek(blk * BLOCK_SIZE)
            buf = bytearray()
            for i, (ino, typ, name) in enumerate(entries):
                is_last = (i == len(entries) - 1)
                nlen = len(name)
                rlen = (8 + nlen + 3) & ~3
                if is_last: rlen = BLOCK_SIZE - len(buf)
                buf += struct.pack('<LHBB', ino, rlen, nlen, typ)
                buf += name.encode()
                buf += b'\x00' * (rlen - 8 - nlen)
            f.write(buf)

    print(f"Populated {filename} from {source_dir}")

if __name__ == "__main__":
    make_ext2(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "build/initramfs-root")
