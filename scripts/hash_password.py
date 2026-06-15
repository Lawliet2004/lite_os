#!/usr/bin/env python3
"""Generate /etc/shadow lines using the same SHA-256 hashing scheme as
libc-lite's pw_hash() so that the in-image /bin/passwd can verify the
pre-seeded passwords.

Output format per line:
    <user>:$5$<salt>$<hex64>:0:0:99999:7:::

Usage:
    python scripts/hash_password.py user1=pass1 user2=pass2 ... > shadow
"""

from __future__ import annotations

import hashlib
import os
import secrets
import sys


ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789./"


def gen_salt(length: int = 8) -> str:
    return "".join(secrets.choice(ALPHABET) for _ in range(length))


def hash_password(password: str, salt: str) -> str:
    """Match user/libc-lite/libc_lite.c::pw_hash exactly.

    digest = SHA256(salt || password)
    for 4999 rounds:
        digest = SHA256(digest || salt)
    return $5$<salt>$<lowercase hex of digest>
    """
    digest = hashlib.sha256((salt + password).encode("utf-8")).digest()
    salt_bytes = salt.encode("utf-8")
    for _ in range(4999):
        digest = hashlib.sha256(digest + salt_bytes).digest()
    return "$5$" + salt + "$" + digest.hex()


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 1
    out_lines: list[str] = []
    for spec in argv[1:]:
        if "=" not in spec:
            print(f"hash_password.py: bad spec {spec!r} (need user=password)", file=sys.stderr)
            return 1
        user, password = spec.split("=", 1)
        salt = os.environ.get("LITENIX_SALT_" + user.upper()) or gen_salt()
        line = f"{user}:{hash_password(password, salt)}:0:0:99999:7:::"
        out_lines.append(line)
    sys.stdout.write("\n".join(out_lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
