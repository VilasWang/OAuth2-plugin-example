#!/bin/bash
# Security Check Script
# 检查是否有敏感文件被意外跟踪或包含敏感信息

# 切换到项目根目录
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

set -e

echo "🔒 OAuth2 Project Security Check"
echo "=================================="
echo ""

ERRORS=0
WARNINGS=0

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 检查函数
check_sensitive_files() {
    echo "📁 Checking for sensitive files in git tracking..."

    # 检查 .env 文件（仅匹配文件名恰为 .env 的文件，排除 paths.env 等合法文件）
    if git ls-files | grep -E "(^|/)\.env$" > /dev/null 2>&1; then
        echo -e "${RED}❌ ERROR: .env files found in git tracking${NC}"
        git ls-files | grep -E "(^|/)\.env$"
        ERRORS=$((ERRORS + 1))
    else
        echo -e "${GREEN}✅ No .env files tracked${NC}"
    fi

    # 检查前端 .env 文件（精确匹配，避免误伤 .env.example）
    if git ls-files | grep -qx "frontends/user/.env" 2>/dev/null; then
        echo -e "${RED}❌ ERROR: frontends/user/.env is tracked${NC}"
        ERRORS=$((ERRORS + 1))
    else
        echo -e "${GREEN}✅ Frontend .env not tracked${NC}"
    fi

    echo ""
}

check_hardcoded_secrets() {
    echo "🔍 Checking for hardcoded secrets in code..."

    # 检查常见的硬编码密钥模式
    PATTERNS=(
        "wx[a-zA-Z0-9]{16,}"           # WeChat AppID
        "sk-[a-zA-Z0-9]{32,}"          # API keys
        "AIza[a-zA-Z0-9_-]{35}"        # Google API keys
        "[a-zA-Z0-9_-]{32,}.*@.*com"   # Possible API keys with domain
    )

    # 排除第三方库和构建目录
    EXCLUDE_DIRS="--exclude-dir=node_modules --exclude-dir=build --exclude-dir=.git --exclude-dir=docs --exclude-dir=dist"

    for pattern in "${PATTERNS[@]}"; do
        if grep -rE "$pattern" --include="*.vue" --include="*.js" --include="*.cc" --include="*.h" \
            $EXCLUDE_DIRS . > /dev/null 2>&1; then
            echo -e "${YELLOW}⚠️  WARNING: Possible hardcoded secrets found${NC}"
            grep -rnE "$pattern" --include="*.vue" --include="*.js" --include="*.cc" --include="*.h" \
                $EXCLUDE_DIRS . 2>/dev/null | head -5
            WARNINGS=$((WARNINGS + 1))
            break
        fi
    done

    if [ $WARNINGS -eq 0 ]; then
        echo -e "${GREEN}✅ No hardcoded secrets detected${NC}"
    fi

    echo ""
}

check_example_files() {
    echo "📝 Checking example files..."

    # 检查 .env.example 是否存在
    if [ -f "frontends/user/.env.example" ]; then
        echo -e "${GREEN}✅ .env.example exists${NC}"

        # 检查是否包含真实凭证（而非示例）
        if grep -E "wx[a-zA-Z0-9]{16,}|sk-[a-zA-Z0-9]{32,}|AIza[a-zA-Z0-9_-]{35}" frontends/user/.env.example > /dev/null 2>&1; then
            echo -e "${RED}❌ ERROR: .env.example may contain real credentials${NC}"
            ERRORS=$((ERRORS + 1))
        fi
    else
        echo -e "${YELLOW}⚠️  WARNING: .env.example not found${NC}"
        WARNINGS=$((WARNINGS + 1))
    fi

    # 检查 config.example.json 是否存在
    if [ -f "frontends/user/public/config.example.json" ]; then
        echo -e "${GREEN}✅ config.example.json exists${NC}"
    else
        echo -e "${YELLOW}⚠️  WARNING: config.example.json not found${NC}"
        WARNINGS=$((WARNINGS + 1))
    fi

    echo ""
}

check_gitignore() {
    echo "🛡️  Checking .gitignore configuration..."

    # 检查关键规则
    REQUIRED=(
        ".env"
        "frontends/user/public/config.json"
    )

    for rule in "${REQUIRED[@]}"; do
        # 容忍 CRLF 行尾（Windows 本地 checkout）
        if tr -d '\r' < .gitignore 2>/dev/null | grep -qx "$rule"; then
            echo -e "${GREEN}✅ $rule is ignored${NC}"
        else
            echo -e "${RED}❌ ERROR: $rule is not in .gitignore${NC}"
            ERRORS=$((ERRORS + 1))
        fi
    done

    echo ""
}

check_file_permissions() {
    echo "🔐 Checking file permissions..."

    # 检查配置文件权限
    if [ -f "apps/server/config/config.json" ]; then
        PERMS=$(stat -c "%a" apps/server/config/config.json 2>/dev/null || stat -f "%A" apps/server/config/config.json)
        echo "   apps/server/config/config.json permissions: $PERMS"
    fi

    echo ""
}

# 运行所有检查
check_sensitive_files
check_hardcoded_secrets
check_example_files
check_gitignore
check_file_permissions

# 总结
echo "=================================="
echo "📊 Security Check Summary"
echo "=================================="

if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo -e "${GREEN}✅ All security checks passed!${NC}"
    exit 0
elif [ $ERRORS -eq 0 ]; then
    echo -e "${YELLOW}⚠️  $WARNINGS warning(s) found${NC}"
    echo "   Please review and fix warnings before committing."
    exit 0
else
    echo -e "${RED}❌ $ERRORS error(s) and $WARNINGS warning(s) found${NC}"
    echo "   Please fix errors before committing."
    exit 1
fi
