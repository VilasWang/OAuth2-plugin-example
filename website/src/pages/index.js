import React from 'react';
import clsx from 'clsx';
import Layout from '@theme/Layout';
import Link from '@docusaurus/Link';
import CodeBlock from '@theme/CodeBlock';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';

import styles from './index.module.css';

const STRINGS = {
  en: {
    title: 'fulla — High-performance open-source IAM core (C++17)',
    description:
      'Production-grade OAuth2/OIDC authorization server and embeddable C++17 SDK: auth-code+PKCE, MFA, WebAuthn, RBAC, multi-tenancy — with admin console, user frontend and official Python/Go clients.',
    badges: ['AGPL-3.0 License', 'Leads all 5 benchmark scenarios', 'Docker · Helm · C++ SDK'],
    taglinePre: 'A ',
    taglineAccent: 'high-performance identity & access core',
    taglinePost: ', built in C++17',
    subtitle:
      'A production-grade OAuth2 / OIDC authorization server that runs out of the box (Docker / Helm) — or embed it into your C++ project as an SDK (find_package(fulla-*)). Ships with an admin console, a user frontend, and official Python / Go clients.',
    ctaDocs: 'Read the Docs',
    ctaQuick: 'Quick Start',
    quickTitle: 'Up and running in five minutes',
    quickText:
      'One command pulls up the full stack: backend (OAuth2/OIDC) + PostgreSQL 17 + Redis + user frontend + admin console + Prometheus. Seed data is built in — sign in to the admin console with admin / admin and walk the whole flow.',
    quickHint: { pre: 'Building from source (Conan 2 + CMake presets, same as CI) — see ', post: '.' },
    quickHintLink: 'Docs · Get started',
    whyTitle: 'Why fulla',
    benchTitle: 'Performance: ahead in all five scenarios',
    benchSubtitlePre:
      'Same machine, same backend, against Keycloak 26 / Ory Hydra v26 / Zitadel v4 (WSL2 8 vCPU · PostgreSQL 17 · wrk ladder · each vendor\u2019s official production config). Steady-state QPS below; \u201cclosest rival\u201d is the best of the three. Full methodology and data: ',
    benchSubtitleLink: 'benchmark report',
    benchSubtitlePost: '.',
    leadPrefix: 'Ahead of the closest rival by ',
    rivalQps: 'closest rival',
    closingTitle: 'Get started',
    closingText:
      'Evaluate the architecture → run it → integrate → deploy to production: every step has a doc, and every key decision has an ADR behind it.',
    closingDocs: 'Open the Docs',
    closingAdr: 'Browse the ADRs',
    features: [
      {
        title: 'High-performance C++17 core',
        description:
          'Async non-blocking Drogon framework with callback-based storage ports. Leads Keycloak, Ory Hydra and Zitadel in all five benchmark scenarios (discovery / client_credentials / introspect / refresh_token / userinfo).',
      },
      {
        title: 'Production-grade OAuth2 / OIDC',
        description:
          'Authorization code + PKCE, client credentials, device flow, refresh rotation with reuse detection, introspection / revocation, OIDC discovery, RP-Initiated Logout, Backchannel Logout.',
      },
      {
        title: 'Embeddable C++ SDK',
        description:
          'The protocol engine is decoupled from the server: the domain layer has zero Drogon dependencies. Take the eight static libraries via find_package(fulla-*) — or add_subdirectory from source; same SDK surface.',
      },
      {
        title: 'Full authentication coverage',
        description:
          'TOTP MFA (second-factor session binding), WebAuthn/Passkeys, GitHub / Google / WeChat social login, progressive account lockout, and anti-enumeration-consistent auth failure handling.',
      },
      {
        title: 'RBAC + granular scopes',
        description:
          'Dual-gate authorization — roles (admin / user / custom) plus resource scopes; roles are issued into JWT claims. Admin APIs cover the full lifecycle of users / clients / roles / scopes / tokens.',
      },
      {
        title: 'Production-ready operations',
        description:
          'Prometheus metrics + six-level structured logging, account-lockout and PG-upgrade runbooks, a deployment acceptance checklist, Redis L2 caching (delayed double-delete consistency), and an official performance baseline.',
      },
    ],
    QUICK_START_COMMENT_1: '# One command, full stack (recommended for evaluation)',
    QUICK_START_COMMENT_2: '#   User frontend    http://localhost:8080',
    QUICK_START_COMMENT_3: '#   Admin console    http://localhost:8081',
    QUICK_START_COMMENT_4: '#   Backend API      http://localhost:5555/.well-known/openid-configuration',
  },
  'zh-CN': {
    title: 'fulla — 高性能开源 IAM 内核（C++17）',
    description:
      '生产级 OAuth2/OIDC 授权服务器 + 可嵌入 C++17 SDK：授权码/PKCE、MFA、WebAuthn、RBAC、多租户，附管理后台、用户前端与官方 Python/Go 客户端。',
    badges: ['AGPL-3.0 License', '五项基准场景全面领先', 'Docker · Helm · C++ SDK'],
    taglinePre: '以 C++17 构建的',
    taglineAccent: '高性能身份与访问管理内核',
    taglinePost: '',
    subtitle:
      '生产级 OAuth2 / OIDC 授权服务器，开箱即用（Docker / Helm）；亦可作为可嵌入 SDK（find_package(fulla-*)）集成进你的 C++ 工程。附管理后台、用户前端与官方 Python / Go 客户端。',
    ctaDocs: '阅读文档',
    ctaQuick: '快速开始',
    quickTitle: '五分钟跑起来',
    quickText:
      '一条命令拉起全栈：后端（OAuth2/OIDC）+ PostgreSQL 17 + Redis + 用户前端 + 管理后台 + Prometheus。种子数据内置，admin / admin 登录管理后台即可体验完整流程。',
    quickHint: { pre: '从源码构建（Conan 2 + CMake presets，与 CI 同构）见 ', post: '。' },
    quickHintLink: '文档 · 开始',
    whyTitle: '为什么选择 fulla',
    benchTitle: '性能：同机同后端，五项场景全部领先',
    benchSubtitlePre:
      '与 Keycloak 26 / Ory Hydra v26 / Zitadel v4 同机对比（WSL2 8 vCPU · PostgreSQL 17 · wrk 阶梯负载 · 各家官方生产配置）。下表为稳态 QPS，“最快竞品”为三者中最高值；完整方法学与数据见 ',
    benchSubtitleLink: '基准对比报告',
    benchSubtitlePost: '。',
    leadPrefix: '领先最快竞品 ',
    rivalQps: '最快竞品',
    closingTitle: '开始使用',
    closingText: '评估架构 → 跑起来 → 集成 → 部署生产，每一步都有对应文档；每个关键设计决策都有 ADR 可溯。',
    closingDocs: '进入文档',
    closingAdr: '浏览 ADR',
    features: [
      {
        title: '高性能 C++17 内核',
        description:
          '异步非阻塞 Drogon 框架 + 回调式存储端口。五项基准场景（discovery / client_credentials / introspect / refresh_token / userinfo）全面领先 Keycloak、Ory Hydra 与 Zitadel。',
      },
      {
        title: '生产级 OAuth2 / OIDC',
        description:
          '授权码 + PKCE、client_credentials、device_code、refresh 轮换与重用检测、introspection / revocation、OIDC 发现文档、RP-Initiated Logout、Backchannel Logout。',
      },
      {
        title: '可嵌入 C++ SDK',
        description:
          '协议引擎与服务器解耦：Domain 层零 Drogon 依赖，find_package(fulla-*) 取走八包静态库，或 add_subdirectory 源码集成——同一 SDK 面。',
      },
      {
        title: '认证与 MFA 全覆盖',
        description:
          'TOTP MFA（第二因子会话绑定）、WebAuthn/Passkeys、GitHub / Google / WeChat 社交登录、账号锁定与防枚举口径统一的认证失败处理。',
      },
      {
        title: 'RBAC + 细粒度 Scope',
        description:
          '角色（admin / user / 自定义角色）与资源 scope 双闸授权模型，roles 已签发进 JWT；管理 API 覆盖用户 / 客户端 / 角色 / scope / token 全生命周期。',
      },
      {
        title: '生产就绪运维面',
        description:
          'Prometheus 指标 + 六级结构化日志、账号锁定与 PG 大版本升级 runbook、部署验收清单、Redis L2 缓存（延迟双删一致性）与官方性能调优基线。',
      },
    ],
    QUICK_START_COMMENT_1: '# Docker Compose 一键起全栈（评估推荐）',
    QUICK_START_COMMENT_2: '#   用户前端    http://localhost:8080',
    QUICK_START_COMMENT_3: '#   管理后台    http://localhost:8081',
    QUICK_START_COMMENT_4: '#   后端 API    http://localhost:5555/.well-known/openid-configuration',
  },
};

const FEATURE_ICONS = [
  // bolt
  <svg key="b" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
    <path d="M13 2L4.5 13.5H11L9.5 22 19 10h-6.5L13 2z" strokeLinejoin="round" />
  </svg>,
  // lock
  <svg key="l" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
    <rect x="4" y="10" width="16" height="10" rx="2" />
    <path d="M8 10V7a4 4 0 018 0v3" strokeLinecap="round" />
  </svg>,
  // code
  <svg key="c" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
    <path d="M8 8l-5 4 5 4M16 8l5 4-5 4M13 5l-2 14" strokeLinecap="round" strokeLinejoin="round" />
  </svg>,
  // shield-check
  <svg key="s" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
    <path d="M12 3l8 4v5c0 5-3.5 8-8 9-4.5-1-8-4-8-9V7l8-4z" strokeLinejoin="round" />
    <path d="M9 12l2 2 4-4" strokeLinecap="round" strokeLinejoin="round" />
  </svg>,
  // users
  <svg key="u" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
    <circle cx="9" cy="8" r="3.2" />
    <path d="M3.5 19c.7-3 2.9-4.5 5.5-4.5S13.8 16 14.5 19" strokeLinecap="round" />
    <circle cx="17" cy="9" r="2.4" />
    <path d="M15.8 13.6c2 .2 3.7 1.5 4.7 4.4" strokeLinecap="round" />
  </svg>,
  // gauge
  <svg key="g" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
    <path d="M4 17a8 8 0 0116 0" strokeLinecap="round" />
    <path d="M12 17l3.5-4" strokeLinecap="round" />
    <circle cx="12" cy="17" r="1.4" />
    <path d="M3 20h18" strokeLinecap="round" />
  </svg>,
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
  const { i18n } = useDocusaurusContext();
  const t = STRINGS[i18n.currentLocale] || STRINGS.en;

  const quickStart = [
    t.QUICK_START_COMMENT_1,
    'docker compose -f deploy/docker/docker-compose.yml up -d --build',
    '',
    t.QUICK_START_COMMENT_2,
    t.QUICK_START_COMMENT_3,
    t.QUICK_START_COMMENT_4,
  ].join('\n');

  const githubBtn = (
    <a className={clsx('button button--secondary button--lg', styles.githubBtn)} href="https://github.com/voidvec/fulla">
      <svg viewBox="0 0 16 16" width="18" height="18" fill="currentColor" aria-hidden="true">
        <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27s1.36.09 2 .27c1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0016 8c0-4.42-3.58-8-8-8z" />
      </svg>
      GitHub
    </a>
  );

  return (
    <Layout title={t.title} description={t.description}>
      <main>
        {/* ---------------- Hero ---------------- */}
        <section className={styles.hero}>
          <div className="container">
            <div className={styles.heroBadges}>
              {t.badges.map((b) => (
                <span key={b} className={styles.badge}>{b}</span>
              ))}
            </div>
            <h1 className={styles.heroTitle}>
              <img src="/img/favicon.svg" alt="fulla" className={styles.heroLogo} />
              fulla
            </h1>
            <p className={styles.heroTagline}>
              {t.taglinePre}
              <span className={styles.heroAccent}>{t.taglineAccent}</span>
              {t.taglinePost}
            </p>
            <p className={styles.heroSubtitle}>{t.subtitle}</p>
            <div className={styles.heroCtas}>
              <Link className="button button--primary button--lg" to="/docs/intro">
                {t.ctaDocs}
              </Link>
              <Link className="button button--secondary button--lg" to="/docs/operate/docker-deployment">
                {t.ctaQuick}
              </Link>
              {githubBtn}
            </div>
          </div>
        </section>

        {/* ---------------- Quick start ---------------- */}
        <section className={styles.section}>
          <div className="container">
            <div className={clsx('row', styles.quickRow)}>
              <div className={clsx('col col--5', styles.quickText)}>
                <h2>{t.quickTitle}</h2>
                <p>{t.quickText}</p>
                <p className={styles.quickHint}>
                  {t.quickHint.pre}
                  <Link to="/docs/intro">{t.quickHintLink}</Link>
                  {t.quickHint.post}
                </p>
              </div>
              <div className={clsx('col col--7', styles.quickCode)}>
                <CodeBlock language="bash">{quickStart}</CodeBlock>
              </div>
            </div>
          </div>
        </section>

        {/* ---------------- Features ---------------- */}
        <section className={clsx(styles.section, styles.featuresSection)}>
          <div className="container">
            <h2 className={styles.sectionTitle}>{t.whyTitle}</h2>
            <div className="row">
              {t.features.map((f, i) => (
                <Feature key={f.title} title={f.title} svg={FEATURE_ICONS[i]} description={f.description} />
              ))}
            </div>
          </div>
        </section>

        {/* ---------------- Benchmark strip ---------------- */}
        <section className={styles.section}>
          <div className="container">
            <h2 className={styles.sectionTitle}>{t.benchTitle}</h2>
            <p className={styles.sectionSubtitle}>
              {t.benchSubtitlePre}
              <a href="https://github.com/voidvec/fulla/blob/master/benchmarks/competitors/results/COMPARISON.md" target="_blank" rel="noopener">
                {t.benchSubtitleLink}
              </a>
              {t.benchSubtitlePost}
            </p>
            <div className={styles.benchRow}>
              {BENCHMARKS.map((b) => (
                <div key={b.scenario} className={styles.benchCard}>
                  <div className={styles.benchScenario}>{b.scenario}</div>
                  <div className={styles.benchQps}>{b.fulla}</div>
                  <div className={styles.benchRatio}>{t.leadPrefix}{b.ratio}</div>
                  <div className={styles.benchBest}>{t.rivalQps} {b.best} QPS</div>
                </div>
              ))}
            </div>
          </div>
        </section>

        {/* ---------------- Closing CTA ---------------- */}
        <section className={clsx(styles.section, styles.closing)}>
          <div className="container">
            <h2>{t.closingTitle}</h2>
            <p>{t.closingText}</p>
            <div className={styles.heroCtas}>
              <Link className="button button--primary button--lg" to="/docs/intro">
                {t.closingDocs}
              </Link>
              <Link className="button button--secondary button--lg" to="/docs/adr/ADR-0001">
                {t.closingAdr}
              </Link>
            </div>
          </div>
        </section>
      </main>
    </Layout>
  );
}
