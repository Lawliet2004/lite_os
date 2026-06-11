#!/usr/bin/env python3
import sys
import os
import subprocess
import shutil

def is_windows():
    return sys.platform == "win32" or os.name == "nt"

def get_file_magic(path):
    try:
        with open(path, "rb") as f:
            return f.read(4)
    except Exception:
        return b""

def check_cmd(cmd, args):
    """Checks if a command is available and runs it with args. Returns (success, version_str_or_error)"""
    # Try resolving path first if not in PATH but maybe in current dir or absolute
    full_path = shutil.which(cmd)
    if not full_path:
        return False, "Not found in PATH"
    
    # Check if this command is the vendored zig and check for host mismatch
    if "toolchain/zig/zig" in cmd.replace("\\", "/"):
        magic = get_file_magic(full_path)
        if is_windows() and magic.startswith(b"\x7fELF"):
            return False, f"Vendored binary is Linux ELF but host is Windows/MSYS. Install zig via pacman or in PATH."
        if not is_windows() and magic.startswith(b"MZ"):
            return False, f"Vendored binary is Windows PE but host is Unix. Install zig via package manager."

    try:
        res = subprocess.run([full_path] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=5)
        out = (res.stdout + res.stderr).strip()
        first_line = out.split("\n")[0] if out else "Executable run succeeded with no output"
        return res.returncode == 0 or res.returncode == 1, first_line
    except subprocess.TimeoutExpired:
        return False, "Command timed out"
    except Exception as e:
        return False, str(e)

def main():
    print("====================================================")
    print("LiteNix Toolchain Verification Check")
    print(f"Host OS: {sys.platform} ({os.name})")
    print("====================================================")
    
    # On Windows, if running under powershell/cmd outside MSYS bash, 
    # we might need to add MSYS2 paths to environment for the subprocess calls to succeed.
    if is_windows():
        msys_paths = [
            r"C:\msys64\usr\bin",
            r"C:\msys64\clang64\bin",
            r"C:\msys64\ucrt64\bin",
            r"C:\msys64\mingw64\bin"
        ]
        existing_path = os.environ.get("PATH", "")
        # Filter existing paths to see if MSYS2 is already there, otherwise prepend
        needs_prepend = not any(p.lower() in existing_path.lower() for p in msys_paths)
        if needs_prepend:
            new_path = ";".join(msys_paths) + ";" + existing_path
            os.environ["PATH"] = new_path
            print("[INFO] Temporarily added MSYS2 directories to PATH for command checking.")

    # Determine ZIG command name
    zig_cmd = "zig"
    # If not on Windows and vendored zig exists, default to it
    if not is_windows() and os.path.exists("toolchain/zig/zig"):
        zig_cmd = "toolchain/zig/zig"
    elif is_windows() and os.path.exists("toolchain/zig/zig.exe"):
        zig_cmd = "toolchain/zig/zig.exe"

    required_tools = {
        "Zig Compiler": (zig_cmd, ["version"]),
        "NASM Assembler": ("nasm", ["-v"]),
        "Xorriso (ISO generator)": ("xorriso", ["--version"]),
        "QEMU Emulator": ("qemu-system-x86_64", ["--version"])
    }

    all_passed = True
    print("\n--- Checking CLI Tools ---")
    for name, (cmd, args) in required_tools.items():
        resolved_path = shutil.which(cmd)
        success, info = check_cmd(cmd, args)
        if success:
            print(f"[OK]  {name:<25}: Found at {resolved_path}")
            print(f"      Version/Info: {info}")
        else:
            print(f"[ERR] {name:<25}: FAILED - {info}")
            if cmd == "zig" or "toolchain/zig" in cmd:
                print("      -> Suggestion: Install Zig 0.11+ or 0.12+ (or mingw-w64-clang-x86_64-zig).")
            elif cmd == "nasm":
                print("      -> Suggestion: Install NASM.")
            elif cmd == "xorriso":
                print("      -> Suggestion: Install xorriso.")
            elif cmd == "qemu-system-x86_64":
                print("      -> Suggestion: Install qemu.")
            all_passed = False

    print("\n--- Checking Limine Bootloader ---")
    limine_file = "toolchain/limine/limine.exe" if is_windows() else "toolchain/limine/limine"
    limine_sys = "toolchain/limine/limine-bios.sys"
    limine_bin = "toolchain/limine/limine-bios-cd.bin"

    for path in [limine_file, limine_sys, limine_bin]:
        if os.path.exists(path):
            print(f"[OK]  Limine File: Found {path}")
        else:
            print(f"[ERR] Limine File: Missing {path}")
            all_passed = False

    print("====================================================")
    if all_passed:
        print("SUCCESS: All toolchain checks passed! LiteNix is ready to build.")
        sys.exit(0)
    else:
        print("ERROR: Some toolchain checks failed. Please see the errors above.")
        sys.exit(1)

if __name__ == "__main__":
    main()
