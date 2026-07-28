#!/bin/bash
# Test Frontend URL Configuration
# 验证前端URL配置是否正确设置

# 切换到项目根目录，确保相对路径引用正确
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

echo "🔍 Frontend URL Configuration Test"
echo "=================================="
echo ""

# 检查配置文件中的前端URL配置
check_config() {
    local config_file=$1
    local expected_url=$2
    local env_name=$3

    if [ -f "$config_file" ]; then
        echo "📋 Testing $env_name configuration:"
        echo "   File: $config_file"

        # 提取frontend.url配置
        frontend_url=$(grep -A 2 '"frontend"' "$config_file" | grep '"url"' | cut -d'"' -f4)

        if [ -n "$frontend_url" ]; then
            echo "   Frontend URL: $frontend_url"

            if [ "$frontend_url" = "$expected_url" ]; then
                echo "   ✅ Matches expected: $expected_url"
            else
                echo "   ⚠️  Expected: $expected_url"
                echo "   ⚠️  Got: $frontend_url"
            fi
        else
            echo "   ❌ Frontend URL not found in configuration"
        fi
        echo ""
    else
        echo "⚠️  Warning: $config_file not found"
        echo ""
    fi
}

# 测试各环境配置
check_config "apps/server/config/config.json" "http://localhost:5173" "Development (Default)"
check_config "apps/server/config/config.dev.json" "http://localhost:5173" "Development"
check_config "apps/server/config/config.ci.json" "http://localhost:5173" "CI"
check_config "apps/server/config/config.prod.json" "https://your-production-domain.com" "Production"

# 检查模板文件是否使用了动态变量
echo "🔍 Checking template file:"
if [ -f "apps/server/views/login.csp" ]; then
    if grep -q "frontend_register_url" apps/server/views/login.csp; then
        echo "   ✅ Template uses dynamic variable: [[frontend_register_url]]"
    else
        echo "   ❌ Template still has hardcoded URL"
    fi

    # 检查是否还有硬编码的localhost:5173
    if grep -q "http://localhost:5173/register" apps/server/views/login.csp; then
        echo "   ❌ Template still contains hardcoded localhost:5173/register"
    else
        echo "   ✅ No hardcoded localhost URL found in template"
    fi
else
    echo "   ⚠️  login.csp not found at apps/server/views/login.csp"
fi

echo ""
echo "📊 Configuration Test Summary"
echo "=================================="
echo "✅ Frontend URL configuration checked"
echo ""
echo "🚀 Next Steps:"
echo "1. Update config.prod.json with actual production frontend URL"
echo "2. Rebuild the backend: ./manage.ps1 build-backend"
echo "3. Test with different configurations"
