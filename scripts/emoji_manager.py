#!/usr/bin/env python3
"""
Emoji management tool - scan and clean emoji from project files.

Usage:
    python emoji_manager.py --scan           # Scan for emoji (default)
    python emoji_manager.py --clean          # Clean emoji from files
    python emoji_manager.py --clean --dry-run # Preview changes without modifying
"""

import argparse
import re
from pathlib import Path

# Emoji to ASCII mapping (using explicit status words)
EMOJI_MAP = {
    # Checkmarks and crosses
    '✓': '[PASS]',
    '✗': '[ERROR]',
    '✔': '[PASS]',
    '✖': '[ERROR]',
    '√': '[PASS]',

    # Warning and info
    '⚠': '[WARNING]',
    '⚡': '[WARNING]',
    '❗': '[WARNING]',
    '❕': '[WARNING]',
    'ℹ': '[INFO]',

    # Common symbols
    '🔍': '[INFO]',   # magnifying glass
    '🔧': '[INFO]',   # wrench
    '🔐': '[AUTH]',   # lock
    '🚀': '[INFO]',   # rocket
    '📋': '===',      # clipboard
    '💡': '[INFO]',   # light bulb
    '🎯': '[>>>]',    # target
    '✨': '[INFO]',   # sparkles
    '🔒': '[LOCK]',   # lock
    '🔓': '[UNLOCK]', # unlock
    '📝': '[INFO]',   # memo
    '📊': '[INFO]',   # chart
    '📈': '[INFO]',   # chart up
    '📉': '[INFO]',   # chart down
    '📄': '[DOC]',    # page
    '📚': '[DOCS]',   # books
    '🔗': '[LINK]',   # link
    '✅': '[PASS]',
    '❌': '[ERROR]',
    '⛔': '[ERROR]',
    '🚫': '[ERROR]',
    '⭕': '[ ]',
    '🔴': '[INFO]',
    '🟢': '[INFO]',
    '🔵': '[INFO]',
    '🟡': '[INFO]',
    '⭐': '[INFO]',
    '💯': '[100%]',
}

# Emoji Unicode ranges (simplified - common ranges)
EMOJI_PATTERNS = [
    # Miscellaneous Symbols (☀, ☁, ☎, etc.)
    re.compile(r'[☀-⛿]'),
    # Dingbats (✀, ✁, ✂, etc.)
    re.compile(r'[✀-➿]'),
    # Emoticons (😀, 😁, 😂, etc.)
    re.compile(r'[\U0001F600-\U0001F64F]'),
    # Transport and Map Symbols (🚀, 🚁, 🚂, etc.)
    re.compile(r'[\U0001F680-\U0001F6FF]'),
    # Miscellaneous Symbols and Pictographs (🀄, 🀅, 🀆, etc.)
    re.compile(r'[\U0001F300-\U0001F5FF]'),
    # Supplemental Symbols and Pictographs (🦀, 🦁, 🦂, etc.)
    re.compile(r'[\U0001F900-\U0001F9FF]'),
]

def has_emoji(text):
    """Check if text contains emoji characters."""
    for pattern in EMOJI_PATTERNS:
        if pattern.search(text):
            return True
    return False

def scan_file(filepath):
    """Scan a single file for emoji and return matches."""
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
            lines = content.split('\n')

            emoji_lines = []
            for i, line in enumerate(lines, 1):
                if has_emoji(line):
                    emoji_lines.append((i, line))

            return emoji_lines
    except Exception as e:
        print(f"Error reading {filepath}: {e}")
        return []

def clean_emoji_from_file(filepath, dry_run=False):
    """Clean emoji from a single file."""
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()

        original_content = content

        # Replace emojis with ASCII alternatives
        for emoji, replacement in EMOJI_MAP.items():
            content = content.replace(emoji, replacement)

        # If content changed, write back
        if content != original_content:
            if dry_run:
                return True, content
            else:
                with open(filepath, 'w', encoding='utf-8') as f:
                    f.write(content)
                return True, None
        return False, None

    except Exception as e:
        print(f"Error processing {filepath}: {e}")
        return False, None

def get_project_files(project_root):
    """Get all project files to scan/clean."""
    cpp_extensions = ['.cc', '.h', '.hpp', '.cpp']
    script_extensions = ['.sh', '.bat', '.ps1', '.py']
    doc_extensions = ['.md', '.txt', 'cmake', 'CMakeLists.txt']

    all_extensions = cpp_extensions + script_extensions + doc_extensions

    files_to_process = []

    # Scan all files
    for ext in all_extensions:
        pattern = f"**/*{ext}" if ext.startswith('.') else f"**/{ext}"
        for filepath in project_root.rglob(pattern):
            # Skip models directory (ORM generated)
            if 'models' in filepath.parts:
                continue
            files_to_process.append(filepath)

    return files_to_process

def scan_mode(project_root):
    """Scan all files for emoji."""
    print("=" * 80)
    print("SCANNING FOR EMOJI IN PROJECT FILES")
    print("=" * 80)

    files_to_process = get_project_files(project_root)
    files_with_emoji = []

    for filepath in files_to_process:
        emoji_lines = scan_file(filepath)
        if emoji_lines:
            files_with_emoji.append((filepath, emoji_lines))

    # Report results
    print(f"\nFound {len(files_with_emoji)} files with emoji:\n")

    for filepath, emoji_lines in files_with_emoji:
        print(f"\n{'='*80}")
        print(f"File: {filepath.relative_to(project_root)}")
        print(f"{'='*80}")
        for line_no, line in emoji_lines:
            # Show line with emoji highlighted (safe encoding)
            try:
                safe_line = line.encode('ascii', 'replace').decode('ascii')
                print(f"  Line {line_no}: {safe_line.strip()}")
            except:
                print(f"  Line {line_no}: [Contains non-ASCII characters]")
            # Find and show the emoji characters
            for pattern in EMOJI_PATTERNS:
                matches = pattern.findall(line)
                if matches:
                    emoji_str = ''.join(matches)
                    try:
                        print(f"    -> Emoji found: {emoji_str}")
                    except:
                        print(f"    -> Emoji found: [Unicode emoji]")

    print(f"\n{'='*80}")
    print("SUMMARY")
    print(f"{'='*80}")
    print(f"Total files scanned: {len(files_to_process)}")
    print(f"Files with emoji: {len(files_with_emoji)}")
    print(f"Files to clean:")
    for filepath, _ in files_with_emoji:
        print(f"  - {filepath.relative_to(project_root)}")

def has_cleanable_emoji(content):
    """Check if content has emoji that can be cleaned (in EMOJI_MAP)."""
    for emoji in EMOJI_MAP.keys():
        if emoji in content:
            return True
    return False

def clean_mode(project_root, dry_run=False):
    """Clean emoji from all files."""
    mode_str = "DRY RUN" if dry_run else "CLEAN"
    print("=" * 80)
    print(f"{mode_str} - EMOJI CLEANING")
    print("=" * 80)

    if dry_run:
        print("[!] DRY RUN MODE: No files will be modified\n")

    files_to_process = get_project_files(project_root)
    cleaned_count = 0
    skipped_count = 0
    error_count = 0

    for filepath in files_to_process:
        # First check if file has emoji
        emoji_lines = scan_file(filepath)
        if not emoji_lines:
            continue

        # Check if emoji can be cleaned
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()

        has_cleanable = has_cleanable_emoji(content)

        relative_path = filepath.relative_to(project_root)

        if not has_cleanable:
            print(f"[-] Skipped (no mapping): {relative_path}")
            skipped_count += 1
            continue

        # Clean the file
        changed, new_content = clean_emoji_from_file(filepath, dry_run=dry_run)

        if changed:
            if dry_run:
                print(f"[+] Would clean: {relative_path}")
            else:
                print(f"[+] Cleaned: {relative_path}")
            cleaned_count += 1

    print(f"\n{'='*80}")
    print(f"{mode_str} SUMMARY")
    print(f"{'='*80}")
    print(f"Total files scanned: {len(files_to_process)}")
    print(f"Files with emoji: {cleaned_count + skipped_count}")
    print(f"Files {'would be ' if dry_run else ''}cleaned: {cleaned_count}")
    print(f"Files skipped (no emoji mapping): {skipped_count}")
    print(f"Errors: {error_count}")

    if skipped_count > 0:
        print(f"\n[*] Skipped files contain emoji not in EMOJI_MAP")
        print(f"[*] Add missing emoji to EMOJI_MAP to clean them")

    if dry_run and cleaned_count > 0:
        print(f"\n[*] Run without --dry-run to apply these changes")

def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description='Emoji management tool - scan and clean emoji from project files',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --scan              Scan for emoji (default)
  %(prog)s --clean             Clean emoji from files
  %(prog)s --clean --dry-run   Preview changes without modifying
        """
    )

    parser.add_argument(
        '--scan', '-s',
        action='store_true',
        help='Scan mode (default)'
    )
    parser.add_argument(
        '--clean', '-c',
        action='store_true',
        help='Clean mode'
    )
    parser.add_argument(
        '--dry-run', '-n',
        action='store_true',
        help='Dry run mode for clean (preview changes without modifying)'
    )
    parser.add_argument(
        '--path', '-p',
        type=str,
        default='..',
        help='Path to project root (default: ..)'
    )

    args = parser.parse_args()

    # Default to scan mode if no mode specified
    if not args.scan and not args.clean:
        args.scan = True

    # Validate dry-run flag
    if args.dry_run and not args.clean:
        parser.error("--dry-run can only be used with --clean")

    project_root = Path(args.path)

    if not project_root.exists():
        print(f"Error: Project root '{project_root}' does not exist")
        return 1

    if args.clean:
        clean_mode(project_root, dry_run=args.dry_run)
    else:
        scan_mode(project_root)

    return 0

if __name__ == "__main__":
    exit(main())
