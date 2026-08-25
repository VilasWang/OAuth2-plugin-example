#!/bin/bash

# 显示帮助信息
show_help() {
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  --all, --clean-images    清理容器、volumes 和镜像"
    echo "  --help, -h               显示此帮助信息"
    echo ""
    echo "默认行为: 只清理容器和 volumes，保留镜像"
    echo ""
    echo "示例:"
    echo "  $0                      # 只清理容器和 volumes"
    echo "  $0 --all               # 清理所有内容（包括镜像）"
    exit 0
}

# 解析参数
CLEAN_IMAGES=false
for arg in "$@"; do
    case $arg in
        --all|--clean-images)
            CLEAN_IMAGES=true
            shift
            ;;
        --help|-h)
            show_help
            ;;
        *)
            echo "❌ 未知参数: $arg"
            echo "使用 --help 查看帮助信息"
            exit 1
            ;;
    esac
done

echo "================================"
echo "OAuth2 Plugin Docker 清理脚本"
echo "================================"
echo ""

# Step 1: 停止并删除所有容器
echo "📦 停止并删除所有容器..."
docker-compose -f ../deploy/docker/docker-compose.yml down --remove-orphans
docker-compose -f ../deploy/docker/docker-compose.debug.yml down --remove-orphans

# Step 2: 删除所有相关的 volumes
echo "🗑️  删除所有相关 volumes..."
docker volume rm oauth2-plugin_postgres_prod 2>/dev/null || echo "  - oauth2-plugin_postgres_prod (不存在或已删除)"
docker volume rm oauth2-plugin_redis_prod 2>/dev/null || echo "  - oauth2-plugin_redis_prod (不存在或已删除)"
docker volume rm oauth2-plugin_prometheus_prod 2>/dev/null || echo "  - oauth2-plugin_prometheus_prod (不存在或已删除)"
docker volume rm oauth2-plugin_build_cache 2>/dev/null || echo "  - oauth2-plugin_build_cache (不存在或已删除)"
docker volume rm oauth2-plugin_postgres_debug 2>/dev/null || echo "  - oauth2-plugin_postgres_debug (不存在或已删除)"
docker volume rm oauth2-plugin_redis_debug 2>/dev/null || echo "  - oauth2-plugin_redis_debug (不存在或已删除)"

# Step 3: 清理镜像（仅在指定参数时执行）
if [ "$CLEAN_IMAGES" = true ]; then
    echo "🖼️  删除项目相关镜像..."
    docker image rm fulla-backend-debug:v1.9.13 2>/dev/null || echo "  - fulla-backend-debug:v1.9.13 (不存在或已删除)"
    docker image rm fulla-backend-frontend 2>/dev/null || echo "  - fulla-backend-frontend (不存在或已删除)"

    echo "🧹 清理悬空镜像和构建缓存..."
    docker image prune -f
    docker builder prune -f
else
    echo "⏭️  跳过镜像清理（使用 --all 参数来清理镜像）"
fi

echo ""
echo "✅ 清理完成！"
echo "📊 当前 Docker 状态:"
echo "  - 容器: $(docker ps -aq | wc -l) 个"
echo "  - 镜像: $(docker images | wc -l) 个"
echo "  - Volumes: $(docker volume ls | wc -l) 个"