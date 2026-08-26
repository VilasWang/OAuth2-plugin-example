import React from 'react';
import clsx from 'clsx';
import Layout from '@theme/Layout';
import Link from '@docusaurus/Link';
import CodeBlock from '@theme/CodeBlock';

import styles from './index.module.css';

const QUICK_START = `# Docker Compose 一键起全栈（评估推荐）
docker compose -f deploy/docker/docker-compose.yml up -d --build

#   用户前端    http://localhost:8080
#   管理后台    http://localhost:8081
#   后端 API    http://localhost:5555/.well-known/openid-configuration`;

const FEATURES = [
  {
    title: '高性能 C++17 内核',
    svg: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
        <path d="M13 2L4.5 13.5H11L9.5 22 19 10h-6.5L13 2z" strokeLinejoin="round" />
      </svg>
    ),
    description: (
      <>异步非阻塞 Drogon 框架 + 回调式存储端口。五项基准场景
        （discovery / client_credentials / introspect / refresh_token / userinfo）
        全面领先 Keycloak、Ory Hydra 与 Zitadel。</>
    ),
  },
  {
    title: '生产级 OAuth2 / OIDC',
    svg: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
        <rect x="4" y="10" width="16" height="10" rx="2" />
        <path d="M8 10V7a4 4 0 018 0v3" strokeLinecap="round" />
      </svg>
    ),
    description: (
      <>授权码 + PKCE、client_credentials、device_code、refresh 轮换与重用检测、
        introspection / revocation、OIDC 发现文档、RP-Initiated Logout、
        Backchannel Logout。</>
    ),
  },
  {
    title: '可嵌入 C++ SDK',
    svg: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
        <path d="M8 8l-5 4 5 4M16 8l5 4-5 4M13 5l-2 14" strokeLinecap="round" strokeLinejoin="round" />
      </svg>
    ),
    description: (
      <>协议引擎与服务器解耦：Domain 层零 Drogon 依赖，
        <code>find_package(fulla-*)</code> 取走八包静态库，
        或 <code>add_subdirectory</code> 源码集成——同一 SDK 面。</>
    ),
  },
  {
    title: '认证与 MFA 全覆盖',
    svg: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
        <path d="M12 3l8 4v5c0 5-3.5 8-8 9-4.5-1-8-4-8-9V7l8-4z" strokeLinejoin="round" />
        <path d="M9 12l2 2 4-4" strokeLinecap="round" strokeLinejoin="round" />
      </svg>
    ),
    description: (
      <>TOTP MFA（第二因子会话绑定）、WebAuthn/Passkeys、GitHub / Google / WeChat
        社交登录、账号锁定与防枚举口径统一的认证失败处理。</>
    ),
  },
  {
    title: 'RBAC + 细粒度 Scope',
    svg: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
        <circle cx="9" cy="8" r="3.2" />
        <path d="M3.5 19c.7-3 2.9-4.5 5.5-4.5S13.8 16 14.5 19" strokeLinecap="round" />
        <circle cx="17" cy="9" r="2.4" />
        <path d="M15.8 13.6c2 .2 3.7 1.5 4.7 4.4" strokeLinecap="round" />
      </svg>
    ),
    description: (
      <>角色（admin / user / org 角色）与资源 scope 双闸授权模型，
        roles 已签发进 JWT；管理 API 覆盖用户 / 客户端 / 角色 / scope / token 全生命周期。</>
    ),
  },
  {
    title: '生产就绪运维面',
    svg: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
        <path d="M4 17a8 8 0 0116 0" strokeLinecap="round" />
        <path d="M12 17l3.5-4" strokeLinecap="round" />
        <circle cx="12" cy="17" r="1.4" />
        <path d="M3 20h18" strokeLinecap="round" />
      </svg>
    ),
    description: (
      <>Prometheus 指标 + 六级结构化日志、账号锁定 runbook、PG 大版本升级 runbook、
        部署验收清单、Redis L2 缓存（延迟双删一致性）与官方性能调优基线。</>
    ),
  },
];

const BENCHMARKS = [
  { scenario: 'discovery', fulla: '87.5k', best: '41.1k', ratio: '2.1×' },
  { scenario: 'client_credentials', fulla: '14.4k', best: '5.6k', ratio: '2.6×' },
  { scenario: 'introspect', fulla: '22.5k', best: '11.5k', ratio: '2.0×' },
  { scenario: 'refresh_token', fulla: '5.5k', best: '2.9k', ratio: '1.9×' },
  { scenario: 'userinfo', fulla: '49.3k', best: '32.7k', ratio: '1.5×' },
];

function Feature({ title, svg, description }) {
  return (
    <div className={clsx('col col--4', styles.featureCard)}>
      <div className={styles.featureIcon}>{svg}</div>
      <h3>{title}</h3>
      <p>{description}</p>
    </div>
  );
}

export default function Home() {
  return (
    <Layout
      title="fulla — 高性能开源 IAM 内核（C++17）"
      description="生产级 OAuth2/OIDC 授权服务器 + 可嵌入 C++17 SDK：授权码/PKCE、MFA、WebAuthn、RBAC、多租户，附管理后台、用户前端与官方 Python/Go 客户端。"
    >
      <main>
        {/* ---------------- Hero ---------------- */}
        <section className={styles.hero}>
          <div className="container">
            <div className={styles.heroBadges}>
              <span className={styles.badge}>MIT License</span>
              <span className={styles.badge}>五项基准场景全面领先</span>
              <span className={styles.badge}>Docker · Helm · C++ SDK</span>
            </div>
            <h1 className={styles.heroTitle}>
              <img src="/img/favicon.svg" alt="fulla" className={styles.heroLogo} />
              fulla
            </h1>
            <p className={styles.heroTagline}>
              以 C++17 构建的<span className={styles.heroAccent}> 高性能身份与访问管理内核</span>
            </p>
            <p className={styles.heroSubtitle}>
              生产级 OAuth2 / OIDC 授权服务器，开箱即用（Docker / Helm）；
              亦可作为可嵌入 SDK（<code>find_package(fulla-*)</code>）集成进你的 C++ 工程。
              附管理后台、用户前端与官方 Python / Go 客户端。
            </p>
            <div className={styles.heroCtas}>
              <Link className="button button--primary button--lg" to="/docs/intro">
                阅读文档
              </Link>
              <Link
                className="button button--secondary button--lg"
                to="/docs/operate/docker-deployment">
                快速开始
              </Link>
              <a
                className={clsx('button button--secondary button--lg', styles.githubBtn)}
                href="https://github.com/voidvec/fulla">
                <svg viewBox="0 0 16 16" width="18" height="18" fill="currentColor" aria-hidden="true">
                  <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27s1.36.09 2 .27c1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0016 8c0-4.42-3.58-8-8-8z" />
                </svg>
                GitHub
              </a>
            </div>
          </div>
        </section>

        {/* ---------------- Quick start ---------------- */}
        <section className={styles.section}>
          <div className="container">
            <div className={clsx('row', styles.quickRow)}>
              <div className={clsx('col col--5', styles.quickText)}>
                <h2>五分钟跑起来</h2>
                <p>
                  一条命令拉起全栈：后端（OAuth2/OIDC）+ PostgreSQL 17 + Redis +
                  用户前端 + 管理后台 + Prometheus。种子数据内置，
                  <code>admin / admin</code> 登录管理后台即可体验完整流程。
                </p>
                <p className={styles.quickHint}>
                  从源码构建（Conan 2 + CMake presets，与 CI 同构）见
                  <Link to="/docs/intro">文档 · 开始</Link>。
                </p>
              </div>
              <div className={clsx('col col--7', styles.quickCode)}>
                <CodeBlock language="bash">{QUICK_START}</CodeBlock>
              </div>
            </div>
          </div>
        </section>

        {/* ---------------- Features ---------------- */}
        <section className={clsx(styles.section, styles.featuresSection)}>
          <div className="container">
            <h2 className={styles.sectionTitle}>为什么选择 fulla</h2>
            <div className="row">
              {FEATURES.map((f) => (
                <Feature key={f.title} {...f} />
              ))}
            </div>
          </div>
        </section>

        {/* ---------------- Benchmark strip ---------------- */}
        <section className={styles.section}>
          <div className="container">
            <h2 className={styles.sectionTitle}>
              性能：同机同后端，五项场景全部领先
            </h2>
            <p className={styles.sectionSubtitle}>
              与 Keycloak 26 / Ory Hydra v26 / Zitadel v4 同机对比
              （WSL2 8 vCPU · PostgreSQL 17 · wrk 阶梯负载 · 各家官方生产配置）。
              下表为稳态 QPS，"最快竞品"为三者中最高值；完整方法学与数据见
              <a href="https://github.com/voidvec/fulla/blob/master/benchmarks/competitors/results/COMPARISON.md" target="_blank" rel="noopener">
                基准对比报告</a>。
            </p>
            <div className={styles.benchRow}>
              {BENCHMARKS.map((b) => (
                <div key={b.scenario} className={styles.benchCard}>
                  <div className={styles.benchScenario}>{b.scenario}</div>
                  <div className={styles.benchQps}>{b.fulla}</div>
                  <div className={styles.benchRatio}>领先最快竞品 {b.ratio}</div>
                  <div className={styles.benchBest}>最快竞品 {b.best} QPS</div>
                </div>
              ))}
            </div>
          </div>
        </section>

        {/* ---------------- Closing CTA ---------------- */}
        <section className={clsx(styles.section, styles.closing)}>
          <div className="container">
            <h2>开始使用</h2>
            <p>
              评估架构 → 跑起来 → 集成 → 部署生产，每一步都有对应文档；
              每个关键设计决策都有 ADR 可溯。
            </p>
            <div className={styles.heroCtas}>
              <Link className="button button--primary button--lg" to="/docs/intro">
                进入文档
              </Link>
              <Link className="button button--secondary button--lg" to="/docs/adr/ADR-0001">
                浏览 ADR
              </Link>
            </div>
          </div>
        </section>
      </main>
    </Layout>
  );
}
