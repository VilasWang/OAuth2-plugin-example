#!/usr/bin/env python3
"""
Docker集成测试报告生成器
生成详细的HTML测试报告，包含健康状态、测试结果、性能指标和故障排除建议
"""

import json
import os
import sys
from datetime import datetime
from pathlib import Path
import argparse


class TestReportGenerator:
    def __init__(self, results_dir, output_file):
        self.results_dir = Path(results_dir)
        self.output_file = Path(output_file)
        self.test_results = {}
        self.health_status = {}
        self.performance_metrics = {}
        self.errors = []

    def load_test_results(self):
        """加载测试结果"""
        results_file = self.results_dir / "test_results.json"
        if results_file.exists():
            with open(results_file, 'r', encoding='utf-8') as f:
                self.test_results = json.load(f)
        else:
            # 创建默认测试结果结构
            self.test_results = {
                "timestamp": datetime.now().isoformat(),
                "total_tests": 0,
                "passed": 0,
                "failed": 0,
                "categories": {}
            }

    def load_health_status(self):
        """加载健康检查结果"""
        health_file = self.results_dir / "health_status.json"
        if health_file.exists():
            with open(health_file, 'r', encoding='utf-8') as f:
                self.health_status = json.load(f)
        else:
            # 创建默认健康状态
            self.health_status = {
                "oauth2-backend": {"status": "unknown", "uptime": "N/A"},
                "oauth2-frontend": {"status": "unknown", "uptime": "N/A"},
                "oauth2-postgres": {"status": "unknown", "uptime": "N/A"},
                "oauth2-redis": {"status": "unknown", "uptime": "N/A"},
                "prometheus": {"status": "unknown", "uptime": "N/A"}
            }

    def generate_html_report(self):
        """生成HTML报告"""
        html_content = self._get_html_template()

        # 填充数据
        html_content = html_content.replace('{{TIMESTAMP}}', datetime.now().strftime('%Y-%m-%d %H:%M:%S'))
        html_content = html_content.replace('{{TOTAL_TESTS}}', str(self.test_results.get('total_tests', 0)))
        html_content = html_content.replace('{{PASSED_TESTS}}', str(self.test_results.get('passed', 0)))
        html_content = html_content.replace('{{FAILED_TESTS}}', str(self.test_results.get('failed', 0)))
        html_content = html_content.replace('{{PASS_RATE}}', f"{self._calculate_pass_rate():.1f}")

        # 健康状态
        health_html = self._generate_health_section()
        html_content = html_content.replace('{{HEALTH_SECTION}}', health_html)

        # 测试结果详情
        results_html = self._generate_test_results_section()
        html_content = html_content.replace('{{TEST_RESULTS_SECTION}}', results_html)

        # 性能指标
        performance_html = self._generate_performance_section()
        html_content = html_content.replace('{{PERFORMANCE_SECTION}}', performance_html)

        # 错误和建议
        troubleshooting_html = self._generate_troubleshooting_section()
        html_content = html_content.replace('{{TROUBLESHOOTING_SECTION}}', troubleshooting_html)

        # 写入文件
        with open(self.output_file, 'w', encoding='utf-8') as f:
            f.write(html_content)

        return str(self.output_file)

    def _calculate_pass_rate(self):
        """计算通过率"""
        total = self.test_results.get('total_tests', 0)
        passed = self.test_results.get('passed', 0)
        if total == 0:
            return 0.0
        return (passed / total) * 100

    def _generate_health_section(self):
        """生成健康状态部分"""
        health_icons = {
            "healthy": "✅",
            "unhealthy": "❌",
            "unknown": "⚠️"
        }

        html = "<div class='health-grid'>"
        for service, status in self.health_status.items():
            icon = health_icons.get(status.get('status', 'unknown'), '⚠️')
            html += f"""
            <div class='health-card {"healthy" if status.get("status") == "healthy" else "unhealthy"}'>
                <div class='health-icon'>{icon}</div>
                <div class='health-service'>{service}</div>
                <div class='health-status'>{status.get('status', 'unknown')}</div>
                <div class='health-uptime'>运行时间: {status.get('uptime', 'N/A')}</div>
            </div>
            """
        html += "</div>"
        return html

    def _generate_test_results_section(self):
        """生成测试结果部分"""
        html = "<div class='test-results'>"
        categories = self.test_results.get('categories', {})

        for category, tests in categories.items():
            html += f"<div class='test-category'>"
            html += f"<h3>{category}</h3>"

            for test in tests.get('tests', []):
                status_class = "passed" if test.get('passed') else "failed"
                status_icon = "✅" if test.get('passed') else "❌"

                html += f"""
                <div class='test-item {status_class}'>
                    <div class='test-status'>{status_icon}</div>
                    <div class='test-name'>{test.get('name', '未知测试')}</div>
                    <div class='test-duration'>{test.get('duration', 'N/A')}</div>
                    <div class='test-message'>{test.get('message', '')}</div>
                """

                if not test.get('passed') and test.get('error'):
                    html += f"<div class='test-error'>错误: {test.get('error')}</div>"

                html += "</div>"

            html += "</div>"

        html += "</div>"
        return html

    def _generate_performance_section(self):
        """生成性能指标部分"""
        html = "<div class='performance-metrics'>"
        html += "<h3>性能指标</h3>"

        metrics = self.test_results.get('performance', {})
        if metrics:
            for metric, value in metrics.items():
                html += f"""
                <div class='metric-item'>
                    <div class='metric-name'>{metric}</div>
                    <div class='metric-value'>{value}</div>
                </div>
                """
        else:
            html += "<p>暂无性能数据</p>"

        html += "</div>"
        return html

    def _generate_troubleshooting_section(self):
        """生成故障排除部分"""
        html = "<div class='troubleshooting'>"
        html += "<h3>故障排除建议</h3>"

        failed_tests = []
        categories = self.test_results.get('categories', {})
        for category, tests in categories.items():
            for test in tests.get('tests', []):
                if not test.get('passed'):
                    failed_tests.append(test)

        if failed_tests:
            for i, test in enumerate(failed_tests[:5]):  # 只显示前5个失败
                html += f"""
                <div class='trouble-item'>
                    <h4>{i+1}. {test.get('name', '未知测试')}</h4>
                    <p><strong>错误:</strong> {test.get('error', '未知错误')}</p>
                    <p><strong>建议:</strong> {self._get_troubleshooting_advice(test)}</p>
                </div>
                """
        else:
            html += "<p>🎉 所有测试通过！没有发现问题。</p>"

        html += "</div>"
        return html

    def _get_troubleshooting_advice(self, test):
        """根据测试类型提供故障排除建议"""
        test_name = test.get('name', '').lower()
        error = test.get('error', '').lower()

        if 'database' in test_name or 'postgres' in test_name:
            return "检查PostgreSQL服务状态，验证数据库连接配置，确认数据库schema已正确初始化。"
        elif 'redis' in test_name:
            return "验证Redis服务运行状态，检查密码配置，确认网络连接正常。"
        elif 'oauth2' in test_name:
            return "检查OAuth2配置，验证客户端ID和密钥，确认重定向URI设置正确。"
        elif 'api' in test_name or 'endpoint' in test_name:
            return "检查后端服务日志，验证API路由配置，确认请求参数正确。"
        elif 'frontend' in test_name or 'vue' in test_name:
            return "检查前端构建配置，验证API端点URL，确认后端CORS设置。"
        else:
            return "查看详细错误日志，检查相关服务配置，验证环境变量设置。"

    def _get_html_template(self):
        """获取HTML模板"""
        return """
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Docker集成测试报告</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            padding: 20px;
            line-height: 1.6;
        }

        .container {
            max-width: 1200px;
            margin: 0 auto;
            background: white;
            border-radius: 10px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.3);
            overflow: hidden;
        }

        .header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 30px;
            text-align: center;
        }

        .header h1 {
            font-size: 2.5em;
            margin-bottom: 10px;
        }

        .summary {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
            padding: 30px;
            background: #f8f9fa;
        }

        .summary-card {
            background: white;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
            text-align: center;
        }

        .summary-card h3 {
            color: #666;
            font-size: 0.9em;
            margin-bottom: 10px;
        }

        .summary-card .number {
            font-size: 2em;
            font-weight: bold;
            color: #667eea;
        }

        .section {
            padding: 30px;
            border-bottom: 1px solid #eee;
        }

        .section h2 {
            color: #333;
            margin-bottom: 20px;
            font-size: 1.8em;
        }

        .health-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
        }

        .health-card {
            background: #f8f9fa;
            padding: 20px;
            border-radius: 8px;
            text-align: center;
            border: 2px solid #ddd;
        }

        .health-card.healthy {
            border-color: #28a745;
            background: #d4edda;
        }

        .health-card.unhealthy {
            border-color: #dc3545;
            background: #f8d7da;
        }

        .health-icon {
            font-size: 2em;
            margin-bottom: 10px;
        }

        .health-service {
            font-weight: bold;
            margin-bottom: 5px;
        }

        .test-results {
            display: flex;
            flex-direction: column;
            gap: 20px;
        }

        .test-category {
            background: #f8f9fa;
            padding: 20px;
            border-radius: 8px;
        }

        .test-item {
            background: white;
            padding: 15px;
            margin: 10px 0;
            border-radius: 5px;
            border-left: 4px solid #ddd;
        }

        .test-item.passed {
            border-left-color: #28a745;
        }

        .test-item.failed {
            border-left-color: #dc3545;
        }

        .test-error {
            background: #f8d7da;
            color: #721c24;
            padding: 10px;
            border-radius: 4px;
            margin-top: 10px;
            font-family: monospace;
        }

        .troubleshooting {
            background: #fff3cd;
            padding: 20px;
            border-radius: 8px;
            border-left: 4px solid #ffc107;
        }

        .trouble-item {
            background: white;
            padding: 15px;
            margin: 10px 0;
            border-radius: 5px;
        }

        .footer {
            background: #f8f9fa;
            padding: 20px;
            text-align: center;
            color: #666;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🐳 Docker集成测试报告</h1>
            <p>OAuth2系统完整集成测试和健康检查</p>
            <p>生成时间: {{TIMESTAMP}}</p>
        </div>

        <div class="summary">
            <div class="summary-card">
                <h3>总测试数</h3>
                <div class="number">{{TOTAL_TESTS}}</div>
            </div>
            <div class="summary-card">
                <h3>通过</h3>
                <div class="number" style="color: #28a745;">{{PASSED_TESTS}}</div>
            </div>
            <div class="summary-card">
                <h3>失败</h3>
                <div class="number" style="color: #dc3545;">{{FAILED_TESTS}}</div>
            </div>
            <div class="summary-card">
                <h3>通过率</h3>
                <div class="number">{{PASS_RATE}}%</div>
            </div>
        </div>

        <div class="section">
            <h2>🏥 服务健康状态</h2>
            {{HEALTH_SECTION}}
        </div>

        <div class="section">
            <h2>🧪 测试结果详情</h2>
            {{TEST_RESULTS_SECTION}}
        </div>

        <div class="section">
            <h2>📊 性能指标</h2>
            {{PERFORMANCE_SECTION}}
        </div>

        <div class="section">
            <h2>🔧 故障排除建议</h2>
            {{TROUBLESHOOTING_SECTION}}
        </div>

        <div class="footer">
            <p>此报告由docker-integration-test技能自动生成</p>
            <p>测试环境: Docker Compose | 测试时间: {{TIMESTAMP}}</p>
        </div>
    </div>
</body>
</html>
        """

def main():
    parser = argparse.ArgumentParser(description='生成Docker集成测试HTML报告')
    parser.add_argument('--test-results', required=True, help='测试结果目录')
    parser.add_argument('--output', required=True, help='输出HTML文件路径')

    args = parser.parse_args()

    generator = TestReportGenerator(args.test_results, args.output)
    generator.load_test_results()
    generator.load_health_status()

    output_path = generator.generate_html_report()
    print(f"Test report generated: {output_path}")

if __name__ == "__main__":
    main()
