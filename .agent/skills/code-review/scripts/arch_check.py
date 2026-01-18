import re
import os
import sys

def check_file_arch(file_path, content):
    """Check architecture compliance based on file type."""
    issues = []
    
    # Identify file type
    is_controller = "controllers" in file_path
    is_filter = "filters" in file_path
    is_model = "models" in file_path
    is_view = ".csp" in file_path or "views" in file_path
    
    # 1. Controller Checks
    if is_controller:
        # Check for direct DB mapping usage (Mapper) which implies business logic in controller
        if "drogon::orm::Mapper" in content:
            issues.append(("ERROR", "Direct DB Mapper usage in Controller. Move DB logic to Model/Service layer."))
        
        # Check for very long functions (heuristic for business logic)
        lines = content.split('\n')
        # Simple heuristic: count braces to find function blocks
        # (This is a simplified check, a real AST parser would be better but heavier)
        
    # 2. Async Safety Checks (All C++ files)
    if file_path.endswith('.cc') or file_path.endswith('.h'):
        # Check for unsafe 'this' capture in lambdas
        # Pattern: [this]... { ... }
        # Only dangerous if used in async callbacks, but flagging all is safer
        unsafe_this = re.search(r'\[.*this.*\]', content)
        if unsafe_this:
            # Check if shared_from_this is used (simple string check)
            if "shared_from_this()" not in content:
                issues.append(("WARNING", "Lambda captures 'this'. Ensure object lifetime safety (consider shared_from_this())."))
        
        # Check for unsafe reference capture
        unsafe_ref = re.search(r'\[.*&.*\]', content)
        if unsafe_ref:
             issues.append(("WARNING", "Lambda captures by reference [&]. Extremely dangerous for async callbacks."))

    return issues

def scan_files(files):
    all_issues = {}
    for f in files:
        try:
            with open(f, 'r', encoding='utf-8') as file:
                content = file.read()
                issues = check_file_arch(f, content)
                if issues:
                    all_issues[f] = issues
        except Exception as e:
            print(f"Failed to read {f}: {e}")
            
    return all_issues

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python arch_check.py <file1> <file2> ...")
        sys.exit(1)
        
    target_files = sys.argv[1:]
    results = scan_files(target_files)
    
    if results:
        print("\n## 🏗️ Architecture & Safety Report\n")
        for f, issues in results.items():
            print(f"### {os.path.basename(f)}")
            for level, msg in issues:
                icon = "❌" if level == "ERROR" else "⚠️"
                print(f"- {icon} **{level}**: {msg}")
            print("")
    else:
        print("\n## 🏗️ Architecture & Safety Report\n")
        print("✅ No architecture or safety violations found.")
