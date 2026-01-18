import sys
import subprocess
import os
from locations import get_tool_path

def run_clang_format(files, tool_path, fix_mode=False):
    print("\n## 🎨 Formatting Check (clang-format)\n")
    
    if fix_mode:
        print(f"> Auto-fixing {len(files)} files...")
        cmd = [tool_path, "-i"] + files
    else:
        cmd = [tool_path, "--dry-run", "--Werror"] + files

    try:
        # Capture output to avoid cluttering unless error
        result = subprocess.run(cmd, capture_output=True, text=True)
        
        if result.returncode == 0:
            if fix_mode:
                 print("✅ Files have been formatted.")
            else:
                 print("✅ All files are properly formatted.")
        else:
            if fix_mode:
                 print("⚠️ Formatting failed on some files:")
            else:
                 print("⚠️ **Formatting Issues Found:**")
            
            if result.stderr:
                print("```")
                print(result.stderr.strip())
                print("```")
            elif not fix_mode:
                 print("Some files need formatting. Run with --fix to apply changes.")
            
    except Exception as e:
        print(f"Error running clang-format: {e}")

def run_cpplint(files, tool_path):
    print("\n## 📏 Style Check (cpplint)\n")
    
    # Simple call to cpplint
    cmd = [sys.executable, format(tool_path)] + files if tool_path.endswith('.py') else [tool_path] + files
    # Note: If installed via pip, it might be a module
    if not os.path.exists(tool_path) and "python" in sys.executable:
         # Try running as module
         cmd = [sys.executable, "-m", "cpplint"] + files

    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            print("✅ Code style looks good.")
        else:
            print("⚠️ **Style Violations:**")
            # Cpplint output is usually on stderr
            output = result.stderr if result.stderr else result.stdout
            
            # Simple parser to format output nicely
            lines = output.split('\n')
            count = 0
            for line in lines:
                if ":" in line and ("error" in line or "warning" in line):
                     print(f"- {line.strip()}")
                     count += 1
                if count > 10:
                    print("- ... (more errors truncated)")
                    break
    except Exception as e:
        print(f"Error running cpplint: {e}")

def run_clang_tidy(files, tool_path):
    print("\n## 🧹 Static Analysis (clang-tidy)\n")
    
    # Clang-tidy is slow, so we just run checking for errors
    # We need compile_commands.json usually. Assuming it exists in build/
    build_path = os.path.join(os.getcwd(), "build")
    cmd = [tool_path, "-p", build_path] + files
    
    try:
        print(f"> Running analysis on {len(files)} files (this may take a while)...")
        result = subprocess.run(cmd, capture_output=True, text=True)
        
        # Filter output to reduce noise
        output_lines = result.stdout.split('\n')
        issues = [line for line in output_lines if "error:" in line or "warning:" in line]
        
        if not issues:
            print("✅ No static analysis issues found.")
        else:
             print(f"⚠️ **Found {len(issues)} potential issues:**")
             for issue in issues[:10]:
                 print(f"- {issue.strip()}")
             if len(issues) > 10:
                 print(f"- ... and {len(issues)-10} more.")

    except Exception as e:
        print(f"Error running clang-tidy: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(1)
    
    files = sys.argv[1:]
    
    # 1. Format
    cf_path = get_tool_path("clang-format")
    if cf_path:
        run_clang_format(files, cf_path)
    else:
        print("⚠️ clang-format not found. Skipping.")

    # 2. Lint
    # Prefer pip installed cpplint
    # 'cpplint' might not resolve if not in path, checking module
    try:
        subprocess.run([sys.executable, "-m", "cpplint", "--version"], capture_output=True, check=True)
        run_cpplint(files, "cpplint") # Handled by module logic above
    except:
         print("\n## 📏 Style Check (cpplint)")
         print("⚠️ cpplint not found. Install via `pip install cpplint` to enable style checks.")

    # 3. Tidy
    ct_path = get_tool_path("clang-tidy")
    if ct_path and os.path.exists("build/compile_commands.json"):
        run_clang_tidy(files, ct_path)
    elif ct_path:
        print("\n## 🧹 Static Analysis (clang-tidy)")
        print("skipped: build/compile_commands.json not found. Run cmake configuration first.")
