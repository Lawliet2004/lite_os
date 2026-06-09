#!/usr/bin/env python3
import sys

def main():
    if len(sys.argv) < 5:
        print("Usage: patch_interpreter.py <input> <output> <old_interp> <new_interp>")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    old_interp = sys.argv[3].encode('utf-8')
    new_interp = sys.argv[4].encode('utf-8')

    with open(input_path, 'rb') as f:
        data = bytearray(f.read())

    idx = data.find(old_interp)
    if idx == -1:
        print(f"Error: Interpreter '{sys.argv[3]}' not found in binary")
        sys.exit(1)

    if len(new_interp) > len(old_interp):
        print("Error: New interpreter path is longer than the original")
        sys.exit(1)

    # Pad with null bytes to preserve size and offset alignment
    padded = new_interp.ljust(len(old_interp), b'\x00')
    data[idx:idx+len(old_interp)] = padded

    with open(output_path, 'wb') as f:
        f.write(data)

    print(f"Patched interpreter '{sys.argv[3]}' to '{sys.argv[4]}' in {output_path}")

if __name__ == '__main__':
    main()
