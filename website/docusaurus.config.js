// fulla.dev — Docusaurus site.
// Core decision (docs/documentation-governance.md §四): the repo's docs/ tree
// IS the site source (docs.path below points one level up). No content copies,
// no sync scripts — a drifted duplicate is exactly what the governance kills.
// Local-only process docs live in docs-local/ (outside this tree, gitignored).

const lightCodeTheme = require('prism-react-renderer').themes.github;
const darkCodeTheme = require('prism-react-renderer').themes.dracula;

/** @type {import('@docusaurus/types').Config} */
const config = {
  title: 'fulla',
  tagline: '高性能开源 IAM 内核 · C++17',
  url: 'https://fulla.dev',
  baseUrl: '/',
  onBrokenLinks: 'throw',
  favicon: 'img/favicon.svg',
  organizationName: 'voidvec',
  projectName: 'fulla',

  // Chinese-primary site (governance v3 §四): docs are simplified Chinese;
  // English lives in README.md on GitHub. i18n ready if an English locale
  // is ever added (Docusaurus native, low switching cost).
  i18n: {
    defaultLocale: 'zh-CN',
    locales: ['zh-CN'],
  },

  presets: [
    [
      'classic',
      /** @type {import('@docusaurus/preset-classic').Options} */
      ({
        docs: {
          path: '../docs',
          routeBasePath: '/docs',
          sidebarPath: require.resolve('./sidebars.js'),
          editUrl: 'https://github.com/voidvec/fulla/edit/master/docs',
          showLastUpdateTime: true,
          exclude: [
            'README.md',
          ],
        },
        blog: false,
        theme: {
          customCss: require.resolve('./src/css/custom.css'),
        },
      }),
    ],
  ],

  plugins: [
    // Offline local search (Chinese via nodejieba + English, no Algolia).
    [
      require.resolve('@cmfcmf/docusaurus-search-local'),
      {
        indexDocs: true,
        indexBlog: false,
        indexPages: true,
        indexDocSidebarParentCategories: 2,
        language: ['en', 'zh'],
      },
    ],
  ],

  markdown: {
    mermaid: true,
  },
  themes: ['@docusaurus/theme-mermaid'],

  themeConfig:
    /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
    ({
      colorMode: {
        defaultMode: 'light',
        disableSwitch: false,
        respectPrefersColorScheme: true,
      },
      navbar: {
        title: 'fulla',
        logo: { alt: 'fulla', src: 'img/favicon.svg' },
        items: [
          { to: '/docs/intro', label: '文档', position: 'left' },
          { to: '/docs/domains/api-reference', label: 'API', position: 'left' },
          {
            href: 'https://github.com/voidvec/fulla/blob/master/benchmarks/competitors/results/COMPARISON.md',
            label: '性能基准',
            position: 'left',
          },
          { to: '/docs/adr/ADR-0001', label: 'ADR', position: 'left' },
          {
            href: 'https://github.com/voidvec/fulla',
            label: 'GitHub',
            position: 'right',
          },
        ],
      },
      footer: {
        style: 'dark',
        links: [
          {
            title: '文档',
            items: [
              { label: '开始', to: '/docs/intro' },
              { label: '架构总览', to: '/docs/architecture/architecture-overview' },
              { label: 'API 参考', to: '/docs/domains/api-reference' },
              { label: 'SDK 集成', to: '/docs/sdk/sdk-integration-guide' },
            ],
          },
          {
            title: '资源',
            items: [
              { label: '生产部署', to: '/docs/operate/deployment' },
              { label: '性能基准', href: 'https://github.com/voidvec/fulla/blob/master/benchmarks/competitors/results/COMPARISON.md' },
              { label: 'OpenAPI 契约', href: 'https://github.com/voidvec/fulla/blob/master/apps/server/openapi.yaml' },
              { label: '架构决策记录', to: '/docs/adr/ADR-0001' },
            ],
          },
          {
            title: '社区',
            items: [
              { label: 'GitHub', href: 'https://github.com/voidvec/fulla' },
              { label: '中文 Wiki', href: 'https://github.com/voidvec/fulla/wiki' },
              { label: '问题反馈', href: 'https://github.com/voidvec/fulla/issues' },
            ],
          },
        ],
        copyright: `Copyright © 2026 Luca · MIT License · 本站内容来自仓库 docs/ 目录（单一事实源）`,
      },
      prism: {
        theme: lightCodeTheme,
        darkTheme: darkCodeTheme,
        additionalLanguages: ['docker', 'powershell'],
      },
      tableOfContents: {
        minHeadingLevel: 2,
        maxHeadingLevel: 4,
      },
    }),
};

module.exports = config;
