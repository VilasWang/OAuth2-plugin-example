#!/usr/bin/env python3
"""
OAuth2 E2E测试脚本
基于test-e2e.md文档执行完整的OAuth2授权码流程测试
"""

import requests
import json
import time
import sys
from typing import Dict, Any, Optional

class OAuth2E2ETester:
    def __init__(self, base_url="http://localhost:5555"):
        self.base_url = base_url
        self.session = requests.Session()
        self.test_results = []
        self.auth_code = None
        self.access_token = None

    def log_test(self, test_name: str, passed: bool, message: str, duration: str = "<1s", error: Optional[str] = None):
        """记录测试结果"""
        result = {
            "name": test_name,
            "passed": passed,
            "duration": duration,
            "message": message
        }
        if error:
            result["error"] = error

        self.test_results.append(result)

        status_icon = "✅" if passed else "❌"
        print(f"{status_icon} {test_name}: {message}")

    def test_login_get_auth_code(self) -> bool:
        """测试登录并获取授权码"""
        print("\n🔐 步骤1: 测试登录获取授权码...")

        login_url = f"{self.base_url}/oauth2/login"
        login_data = {
            "username": "admin",
            "password": "admin",
            "client_id": "vue-client",
            "redirect_uri": "http://localhost:5173/callback",
            "scope": "openid",
            "state": "e2e_test",
            "response_type": "code"
        }

        try:
            start_time = time.time()
            response = self.session.post(
                login_url,
                data=login_data,
                allow_redirects=False,
                timeout=10
            )
            duration = f"{(time.time() - start_time)*1000:.0f}ms"

            if response.status_code in [302, 301]:
                location = response.headers.get('Location', '')
                print(f"   重定向到: {location}")

                # 提取授权码
                import re
                code_match = re.search(r'code=([^&]+)', location)
                if code_match:
                    self.auth_code = code_match.group(1)
                    print(f"   获得授权码: {self.auth_code[:20]}...")
                    self.log_test("OAuth2登录获取授权码", True, f"成功获取授权码", duration)
                    return True
                else:
                    self.log_test("OAuth2登录获取授权码", False, "重定向中未找到授权码", duration,
                                f"重定向URL: {location}")
                    return False
            else:
                self.log_test("OAuth2登录获取授权码", False, f"意外的状态码: {response.status_code}", duration,
                            f"响应: {response.text[:200]}")
                return False

        except Exception as e:
            self.log_test("OAuth2登录获取授权码", False, "登录请求失败", "<1s", str(e))
            return False

    def test_exchange_token(self) -> bool:
        """测试使用授权码交换token"""
        if not self.auth_code:
            print("❌ 没有授权码，跳过token交换测试")
            return False

        print("\n🎫 步骤2: 测试授权码交换token...")

        token_url = f"{self.base_url}/oauth2/token"
        token_data = {
            "grant_type": "authorization_code",
            "code": self.auth_code,
            "client_id": "vue-client",
            "client_secret": "123456",
            "redirect_uri": "http://localhost:5173/callback"
        }

        try:
            start_time = time.time()
            response = self.session.post(token_url, data=token_data, timeout=10)
            duration = f"{(time.time() - start_time)*1000:.0f}ms"

            if response.status_code == 200:
                token_data = response.json()
                if 'access_token' in token_data:
                    self.access_token = token_data['access_token']
                    print(f"   获得访问令牌: {self.access_token[:20]}...")

                    # 检查角色
                    roles = token_data.get('roles', [])
                    if 'admin' in roles:
                        print(f"   ✅ 角色验证通过: {roles}")
                        self.log_test("OAuth2 Token交换", True, f"成功获取token，包含admin角色", duration)
                        return True
                    else:
                        print(f"   ⚠️ 角色中缺少admin: {roles}")
                        self.log_test("OAuth2 Token交换", True, f"成功获取token，但角色: {roles}", duration)
                        return True
                else:
                    self.log_test("OAuth2 Token交换", False, "响应中没有access_token", duration,
                                f"响应: {json.dumps(token_data, indent=2)}")
                    return False
            else:
                self.log_test("OAuth2 Token交换", False, f"Token交换失败: {response.status_code}", duration,
                            f"响应: {response.text[:200]}")
                return False

        except Exception as e:
            self.log_test("OAuth2 Token交换", False, "Token交换请求失败", "<1s", str(e))
            return False

    def test_access_protected_resource(self) -> bool:
        """测试访问受保护的管理资源"""
        if not self.access_token:
            print("❌ 没有访问令牌，跳过受保护资源测试")
            return False

        print("\n🛡️ 步骤3: 测试访问受保护的管理资源...")

        admin_url = f"{self.base_url}/api/admin/dashboard"
        headers = {
            "Authorization": f"Bearer {self.access_token}"
        }

        try:
            start_time = time.time()
            response = self.session.get(admin_url, headers=headers, timeout=10)
            duration = f"{(time.time() - start_time)*1000:.0f}ms"

            if response.status_code == 200:
                data = response.json()
                if data.get('status') == 'success':
                    print(f"   ✅ 管理面板访问成功")
                    print(f"   响应: {json.dumps(data, indent=2, ensure_ascii=False)}")
                    self.log_test("管理面板访问", True, "成功访问管理面板", duration)
                    return True
                else:
                    print(f"   ⚠️ 意外的响应状态: {data}")
                    self.log_test("管理面板访问", True, f"访问成功但响应: {data}", duration)
                    return True
            else:
                self.log_test("管理面板访问", False, f"访问失败: {response.status_code}", duration,
                            f"响应: {response.text[:200]}")
                return False

        except Exception as e:
            self.log_test("管理面板访问", False, "管理面板访问失败", "<1s", str(e))
            return False

    def run_all_tests(self) -> Dict[str, Any]:
        """运行所有E2E测试"""
        print("🧪 开始OAuth2 E2E测试...")
        print("=" * 50)

        # 步骤1: 登录获取授权码
        step1_passed = self.test_login_get_auth_code()

        # 步骤2: 交换token
        step2_passed = self.test_exchange_token()

        # 步骤3: 访问受保护资源
        step3_passed = self.test_access_protected_resource()

        # 汇总结果
        total_tests = len(self.test_results)
        passed_tests = sum(1 for result in self.test_results if result['passed'])
        failed_tests = total_tests - passed_tests

        print("\n" + "=" * 50)
        print("📊 OAuth2 E2E测试结果:")
        print(f"   总测试数: {total_tests}")
        print(f"   通过: {passed_tests}")
        print(f"   失败: {failed_tests}")
        print(f"   通过率: {(passed_tests/total_tests*100):.1f}%" if total_tests > 0 else "   通过率: N/A")

        return {
            "category": "OAuth2 E2E测试",
            "description": "完整OAuth2授权码流程和权限验证",
            "tests": self.test_results,
            "summary": {
                "total": total_tests,
                "passed": passed_tests,
                "failed": failed_tests,
                "pass_rate": (passed_tests/total_tests*100) if total_tests > 0 else 0
            }
        }

def main():
    """主函数"""
    import argparse

    parser = argparse.ArgumentParser(description='OAuth2 E2E测试')
    parser.add_argument('--base-url', default='http://localhost:5555', help='OAuth2服务器URL')
    parser.add_argument('--output', default='test-results/oauth2_e2e_results.json', help='输出结果文件')

    args = parser.parse_args()

    # 创建测试器
    tester = OAuth2E2ETester(args.base_url)

    # 运行测试
    results = tester.run_all_tests()

    # 保存结果
    import os
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, 'w', encoding='utf-8') as f:
        json.dump(results, f, indent=2, ensure_ascii=False)

    print(f"\n📄 测试结果已保存到: {args.output}")

    # 返回退出码
    sys.exit(0 if results['summary']['failed'] == 0 else 1)

if __name__ == "__main__":
    main()
