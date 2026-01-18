import shutil
import os
import subprocess
import glob

def find_vs_llvm_tool(tool_name):
    """Find tool in Visual Studio installation if not in PATH."""
    # List of common VS editions and years to check
    vs_years = ['2022', '2019', '2017']
    vs_editions = ['Enterprise', 'Professional', 'Community', 'BuildTools']
    
    base_path = r"C:\Program Files\Microsoft Visual Studio"
    
    # Try to find via glob first (more robust)
    pattern = os.path.join(base_path, "*", "*", "VC", "Tools", "Llvm", "**", "bin", f"{tool_name}.exe")
    matches = glob.glob(pattern, recursive=True)
    if matches:
        return matches[0]
        
    return None

def get_tool_path(tool_name):
    """Get the absolute path of a tool."""
    # 1. Check PATH
    path = shutil.which(tool_name)
    if path:
        return path
        
    # 2. Check VS Installation (for clang tools)
    if tool_name.startswith("clang"):
        vs_path = find_vs_llvm_tool(tool_name)
        if vs_path:
            return vs_path
            
    return None

if __name__ == "__main__":
    tools = ["clang-format", "clang-tidy", "cpplint", "ctest"]
    print("Found tools:")
    for tool in tools:
        path = get_tool_path(tool)
        status = path if path else "NOT FOUND"
        print(f"{tool}: {status}")
