import sys
import os
import subprocess

# Add current dir to path to find sibling scripts
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

import locations
import static_check
import arch_check
# Removed: import run_tests (use /test workflow instead)

def find_all_sources(root_dir="."):
    exts = ('.cc', '.h', '.cpp', '.hpp', '.csp')
    srcs = []
    for root, dirs, fs in os.walk(root_dir):
        if "build" in root or ".git" in root or "third_party" in root or ".agent" in root:
             continue
        for f in fs:
            if f.endswith(exts):
                srcs.append(os.path.join(root, f))
    return srcs

def main():
    args = sys.argv[1:]
    files = []
    
    # 1. Determine Target Files
    if "--all" in args:
        files = find_all_sources()
    elif len(args) > 0 and not args[0].startswith("-"):
        files = args # Assume file list provided as args
    else:
        # Default: Git Diff
        try:
            # Get staged
            staged = subprocess.check_output(["git", "diff", "--name-only", "--cached"], text=True).splitlines()
            # Get unstaged
            unstaged = subprocess.check_output(["git", "diff", "--name-only"], text=True).splitlines()
            
            candidates = set(staged + unstaged)
            exts = ('.cc', '.h', '.cpp', '.hpp', '.csp')
            files = [f for f in candidates if f.endswith(exts) and os.path.exists(f)]
        except Exception as e:
            # print(f"Git diff failed: {e}")
            pass

    if not files:
        if "--all" not in args:
            print("ℹ️ No modified source files found. Use --all to check everything.")
            print("   Use /test workflow to run tests.")
            return
        else:
             print("ℹ️ No source files found in project.")
             return

    # Exclude models directory as requested (ORM generated code)
    files = [f for f in files if "models" not in os.path.normpath(f).split(os.sep)]

    # Redirect output to report file if requested
    if "--report" in args:
        import datetime

        timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        report_file = os.path.join("tmp_output", "reports", f"review_{timestamp}.md")
        
        # Make sure directory exists
        os.makedirs(os.path.dirname(report_file), exist_ok=True)
        
        print(f"📝 Report will be saved to: {report_file}")
        sys.stdout = open(report_file, "w", encoding='utf-8')
        print(f"# Code Review Report ({timestamp})\n")

    print(f"🔍 Reviewing {len(files)} files: {', '.join(files[:3])}..." + (" (and others)" if len(files)>3 else ""))

    # 2. Static Analysis
    # 2.1 Clang Format
    cf_path = locations.get_tool_path("clang-format")
    fix_required = "--fix" in args
    
    if cf_path:
        static_check.run_clang_format(files, cf_path, fix_mode=fix_required)
    else:
        print("⚠️ clang-format not found.")

    # 2.2 Cpplint
    try:
        subprocess.run([sys.executable, "-m", "cpplint", "--version"], capture_output=True, check=True)
        static_check.run_cpplint(files, "cpplint")
    except:
        print("\n## 📏 Style Check (cpplint)")
        print("⚠️ cpplint module not found. Run `pip install cpplint`.")

    # 3. Architecture & Safety
    arch_issues = arch_check.scan_files(files)
    print("\n## 🏗️ Architecture & Safety Report\n")
    if arch_issues:
        for f, issues in arch_issues.items():
            print(f"### {os.path.basename(f)}")
            for level, msg in issues:
                icon = "❌" if level == "ERROR" else "⚠️"
                print(f"- {icon} **{level}**: {msg}")
    else:
        print("✅ No architecture checks failed.")

    # Note: Tests are handled by /test workflow, not duplicated here
    print("\nℹ️ Use `/test` workflow to run tests.")

if __name__ == "__main__":
    main()
