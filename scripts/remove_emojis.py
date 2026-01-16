#!/usr/bin/env python3
"""
FlowCoro Emoji Removal Tool
Removes all emoji symbols from code and documentation files.
"""

import os
import re
import sys
import argparse
from pathlib import Path
from typing import List, Tuple

# Comprehensive Unicode emoji pattern
EMOJI_PATTERN = re.compile(
    '['
    '\U0001F600-\U0001F64F'  # emoticons
    '\U0001F300-\U0001F5FF'  # symbols & pictographs
    '\U0001F680-\U0001F6FF'  # transport & map symbols
    '\U0001F1E0-\U0001F1FF'  # flags (iOS)
    '\U00002702-\U000027B0'  # dingbats
    '\U000024C2-\U0001F251'
    '\U00002600-\U000026FF'  # miscellaneous symbols
    '\U0001F900-\U0001F9FF'  # supplemental symbols
    '\U0001FA00-\U0001FA6F'  # chess symbols
    '\U0001FA70-\U0001FAFF'  # symbols and pictographs extended-a
    '\U00002B50'              # white star
    '\U0001F004-\U0001F0CF'  # mahjong and playing cards
    ']+',
    flags=re.UNICODE
)

# File extensions to process
FILE_EXTENSIONS = {
    '.cpp', '.hpp', '.h', '.c', '.cc', '.cxx',
    '.md', '.txt', '.rst',
    '.cmake',
    '.py', '.sh', '.bash',
    '.json', '.yaml', '.yml',
    '.log'
}

# Special file names to include
SPECIAL_FILES = {'CMakeLists.txt'}

# Directories to exclude
EXCLUDED_DIRS = {'.git', '.vscode', 'build', '.cache', '__pycache__', 'node_modules', 'pgo_profiles'}


class Colors:
    """ANSI color codes for terminal output."""
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    NC = '\033[0m'


def log(message: str):
    """Print info message."""
    print(f"{Colors.BLUE}[INFO]{Colors.NC} {message}")


def warn(message: str):
    """Print warning message."""
    print(f"{Colors.YELLOW}[WARN]{Colors.NC} {message}")


def error(message: str):
    """Print error message."""
    print(f"{Colors.RED}[ERROR]{Colors.NC} {message}")


def success(message: str):
    """Print success message."""
    print(f"{Colors.GREEN}[SUCCESS]{Colors.NC} {message}")


def should_process_file(file_path: Path) -> bool:
    """Check if file should be processed."""
    # Check if in excluded directory
    for part in file_path.parts:
        if part in EXCLUDED_DIRS:
            return False
    
    # Check file extension or special name
    return (file_path.suffix in FILE_EXTENSIONS or 
            file_path.name in SPECIAL_FILES)


def count_emojis(text: str) -> int:
    """Count emoji occurrences in text."""
    return len(EMOJI_PATTERN.findall(text))


def remove_emojis(text: str) -> str:
    """Remove all emojis from text."""
    return EMOJI_PATTERN.sub('', text)


def process_file(file_path: Path, dry_run: bool = True, backup: bool = False, verbose: bool = False) -> Tuple[bool, int]:
    """
    Process a single file to remove emojis.
    
    Returns:
        Tuple of (modified, emoji_count)
    """
    try:
        # Try to read file with UTF-8 encoding
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
        except UnicodeDecodeError:
            # Fallback to other encodings
            with open(file_path, 'r', encoding='latin-1') as f:
                content = f.read()
        
        # Count emojis
        emoji_count = count_emojis(content)
        
        if emoji_count == 0:
            if verbose:
                log(f"{file_path}: No emojis found")
            return False, 0
        
        # Remove emojis
        cleaned_content = remove_emojis(content)
        
        if dry_run:
            warn(f"[DRY RUN] {file_path}: Would remove {emoji_count} emoji(s)")
        else:
            # Backup if requested
            if backup:
                backup_path = Path(str(file_path) + '.bak')
                with open(backup_path, 'w', encoding='utf-8') as f:
                    f.write(content)
                if verbose:
                    log(f"Backed up to: {backup_path}")
            
            # Write cleaned content
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write(cleaned_content)
            success(f"{file_path}: Removed {emoji_count} emoji(s)")
        
        return True, emoji_count
    
    except Exception as e:
        error(f"Failed to process {file_path}: {e}")
        return False, 0


def find_files(root_dir: Path) -> List[Path]:
    """Find all files that should be processed."""
    files = []
    for path in root_dir.rglob('*'):
        if path.is_file() and should_process_file(path):
            files.append(path)
    return files


def main():
    parser = argparse.ArgumentParser(
        description='FlowCoro Emoji Removal Tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                    # Dry run mode (default)
  %(prog)s --apply            # Actually remove emojis
  %(prog)s --apply --backup   # Remove emojis and backup files
  %(prog)s --verbose          # Verbose dry run
        """
    )
    parser.add_argument('--dry-run', action='store_true', default=True,
                        help='Dry run mode, do not modify files (default)')
    parser.add_argument('--apply', action='store_true',
                        help='Actually apply changes (overrides --dry-run)')
    parser.add_argument('--backup', action='store_true',
                        help='Backup original files before modification')
    parser.add_argument('--directory', type=Path, default=Path.cwd(),
                        help='Directory to process (default: current directory)')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Verbose output')
    
    args = parser.parse_args()
    
    # Determine run mode
    dry_run = not args.apply
    
    # Print configuration
    print("=" * 60)
    print(" FlowCoro Emoji Removal Tool")
    print("=" * 60)
    print(f"Target directory: {args.directory.absolute()}")
    print(f"Run mode: {'DRY RUN' if dry_run else 'APPLY CHANGES'}")
    print(f"Backup: {'Yes' if args.backup else 'No'}")
    print(f"Verbose: {'Yes' if args.verbose else 'No'}")
    print()
    
    # Confirmation for actual execution
    if not dry_run:
        warn("About to remove emojis from files. This will modify file contents!")
        if args.backup:
            log("Backup files will be created")
        print()
        response = input("Confirm continue? (y/N): ")
        if response.lower() != 'y':
            error("Operation cancelled")
            return 1
        print()
    else:
        warn("Running in DRY RUN mode - no files will be modified")
        log("Use --apply to actually execute changes")
        print()
    
    # Find files to process
    log("Scanning for files...")
    files = find_files(args.directory)
    log(f"Found {len(files)} files to check")
    print()
    
    # Process files
    total_files = 0
    modified_files = 0
    total_emojis = 0
    
    for file_path in files:
        total_files += 1
        modified, count = process_file(file_path, dry_run, args.backup, args.verbose)
        if modified:
            modified_files += 1
            total_emojis += count
    
    # Print summary
    print()
    print("=" * 60)
    print(" Summary")
    print("=" * 60)
    print(f"Total files checked: {total_files}")
    print(f"Files with emojis: {modified_files}")
    print(f"Total emojis: {total_emojis}")
    print()
    
    if dry_run:
        warn("This was a DRY RUN - no files were modified")
        log("Use --apply to actually execute changes")
    else:
        success("Emoji removal completed!")
        if args.backup:
            log("Original files backed up with .bak extension")
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
