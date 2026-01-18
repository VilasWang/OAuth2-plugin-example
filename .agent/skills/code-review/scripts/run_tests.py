import sys
import subprocess
import os

def run_tests():
    print("\n## 🧪 Automated Tests\n")
    
    # 1. Check for build directory
    build_dir = os.path.join(os.getcwd(), "OAuth2Backend", "build")
    if not os.path.exists(build_dir):
        # Fallback to root build dir if exists
        build_dir = os.path.join(os.getcwd(), "build")
        
    if not os.path.exists(build_dir):
        print("❌ Build directory not found. Cannot run tests.")
        return

    # 2. Run CTest
    print(f"> Running CTest in {build_dir}...")
    try:
        # Determine config (Release/Debug) - heuristics based on folders
        config = "Release"
        if os.path.exists(os.path.join(build_dir, "Debug")) and not os.path.exists(os.path.join(build_dir, "Release")):
            config = "Debug"
            
        cmd = ["ctest", "-C", config, "--output-on-failure"]
        result = subprocess.run(cmd, cwd=build_dir, capture_output=True, text=True)
        
        if result.returncode == 0:
            print(f"✅ CTest ({config}) Passed!")
            # Optional: print summary
            for line in result.stdout.split('\n'):
                if "Test project" in line or "% tests passed" in line:
                    print(f"  - {line.strip()}")
        else:
            print("❌ **Tests Failed:**")
            print("```")
            # Limit output length
            lines = result.stdout.split('\n')
            if len(lines) > 20:
                print("\n".join(lines[-20:]))
            else:
                print(result.stdout)
            print("```")
            
    except FileNotFoundError:
        print("⚠️ CTest not found in PATH.")
    except Exception as e:
        print(f"Error running tests: {e}")

if __name__ == "__main__":
    run_tests()
