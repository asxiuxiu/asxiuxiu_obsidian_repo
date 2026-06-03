#!/usr/bin/env python3
"""
Session archive tool: compress + encrypt / decrypt + decompress
Uses OpenSSL AES-256-CBC + PBKDF2 + zlib

Usage:
    python session_tool.py archive-all          # encrypt all sessions to archive/
    python session_tool.py decrypt-all          # decrypt all archives back to sessions/
    python session_tool.py encrypt --in a.jsonl --out a.enc
    python session_tool.py decrypt --in a.enc --out a.jsonl
"""

import argparse
import os
import sys
import zlib
import subprocess

SESSION_DIR = ".practice-tracker/sessions"
ARCHIVE_DIR = ".practice-tracker/archive"
KEY_FILE = ".practice-tracker/.key"


def get_key():
    """Get encryption key from env var or local key file. Auto-generate if absent."""
    env_key = os.environ.get("CPP_RECOVERY_KEY")
    if env_key:
        return env_key
    if os.path.exists(KEY_FILE):
        with open(KEY_FILE, "r", encoding="utf-8") as f:
            return f.read().strip()
    # Auto-generate a random key
    import secrets
    key = secrets.token_hex(32)
    with open(KEY_FILE, "w", encoding="utf-8") as f:
        f.write(key)
    print(
        f"[!] 新生成密钥已保存到 {KEY_FILE}", file=sys.stderr)
    print(
        f"[!] 建议：将密钥备份到安全位置，或设置环境变量 CPP_RECOVERY_KEY", file=sys.stderr)
    print(
        f"[!] 注意：{KEY_FILE} 已被 .gitignore 忽略，不会上传 git", file=sys.stderr)
    return key


def encrypt_file(in_path: str, out_path: str) -> None:
    key = get_key()
    with open(in_path, "rb") as f:
        raw = f.read()
    compressed = zlib.compress(raw, level=9)

    proc = subprocess.run(
        ["openssl", "enc", "-aes-256-cbc", "-pbkdf2", "-salt",
         "-pass", f"pass:{key}"],
        input=compressed,
        capture_output=True
    )
    if proc.returncode != 0:
        print(f"[!] 加密失败: {proc.stderr.decode()}", file=sys.stderr)
        sys.exit(1)

    with open(out_path, "wb") as f:
        f.write(proc.stdout)
    ratio = len(proc.stdout) / len(raw) * 100 if raw else 0
    print(f"[+] {os.path.basename(in_path)}: {len(raw)} -> {len(proc.stdout)} bytes ({ratio:.1f}%)")


def decrypt_file(in_path: str, out_path: str) -> None:
    key = get_key()
    with open(in_path, "rb") as f:
        encrypted = f.read()

    proc = subprocess.run(
        ["openssl", "enc", "-aes-256-cbc", "-pbkdf2", "-salt", "-d",
         "-pass", f"pass:{key}"],
        input=encrypted,
        capture_output=True
    )
    if proc.returncode != 0:
        print(f"[!] 解密失败: {proc.stderr.decode()}", file=sys.stderr)
        sys.exit(1)

    raw = zlib.decompress(proc.stdout)
    with open(out_path, "wb") as f:
        f.write(raw)
    print(f"[+] {os.path.basename(in_path)} -> {out_path} ({len(raw)} bytes)")


def archive_all():
    os.makedirs(ARCHIVE_DIR, exist_ok=True)
    files = [f for f in os.listdir(SESSION_DIR) if f.endswith(".jsonl")]
    if not files:
        print("[!] sessions/ 目录下没有 .jsonl 文件", file=sys.stderr)
        return
    for fname in files:
        in_path = os.path.join(SESSION_DIR, fname)
        out_path = os.path.join(ARCHIVE_DIR, fname + ".enc")
        encrypt_file(in_path, out_path)
    print(f"[+] 共归档 {len(files)} 个会话文件到 {ARCHIVE_DIR}/")


def decrypt_all():
    os.makedirs(SESSION_DIR, exist_ok=True)
    files = [f for f in os.listdir(ARCHIVE_DIR) if f.endswith(".enc")]
    if not files:
        print("[!] archive/ 目录下没有 .enc 文件", file=sys.stderr)
        return
    for fname in files:
        in_path = os.path.join(ARCHIVE_DIR, fname)
        out_path = os.path.join(SESSION_DIR, fname[:-4])  # strip .enc
        decrypt_file(in_path, out_path)
    print(f"[+] 共还原 {len(files)} 个会话文件到 {SESSION_DIR}/")


def main():
    parser = argparse.ArgumentParser(
        description="Session archive: zlib + AES-256-CBC")
    parser.add_argument(
        "action",
        choices=["archive-all", "decrypt-all", "encrypt", "decrypt"])
    parser.add_argument("--in", dest="in_path", help="Input file")
    parser.add_argument("--out", dest="out_path", help="Output file")
    args = parser.parse_args()

    if args.action == "archive-all":
        archive_all()
    elif args.action == "decrypt-all":
        decrypt_all()
    elif args.action == "encrypt":
        if not args.in_path or not args.out_path:
            parser.error("encrypt requires --in and --out")
        encrypt_file(args.in_path, args.out_path)
    elif args.action == "decrypt":
        if not args.in_path or not args.out_path:
            parser.error("decrypt requires --in and --out")
        decrypt_file(args.in_path, args.out_path)


if __name__ == "__main__":
    main()
