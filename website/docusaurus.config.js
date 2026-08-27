// fulla.dev — Docusaurus site.
// Core decision (docs/documentation-governance.md §4): the repo's docs trees
// ARE the site source (docs.path below points one level up for the default
// en locale; the zh-CN translation tree lives under website/i18n/zh-CN/).
// No content copies between locales' structure — same layout, same URLs.
// Local-only process docs live in docs-local/ (outside the trees, gitignored).

const lightCodeTheme = require('prism-react-renderer').themes.github;
const darkCodeTheme = require('prism-react-renderer').themes.dracula;
// Locale-aware labels for config strings: Docusaurus sets DOCUSAURUS_CURRENT_LOCALE
// when loading the config for each locale's build pass.
const CURRENT_LOCALE = process.env.DOCUSAURUS_CURRENT_LOCALE || 'en';
const L = (en, zh) => (CURRENT_LOCALE === 'zh-CN' ? zh : en);

/** @type {import('@docusaurus/types').Config} */
const config = {
  title: 'fulla',
  tagline: 'High-performance open-source IAM core · C++17',
  url: 'https://fulla.dev',
  baseUrl: '/',
  onBrokenLinks: 'throw',
  favicon: 'img/favicon.svg',
  organizationName: 'voidvec',
  projectName: 'fulla',

  // English-primary site (governance v4): '/' serves English, '/zh-CN'
  // serves the Chinese translation, navbar carries a locale switcher.
  // Both locales build in CI; translation duty = same-PR dual writes.
  i18n: {
    defaultLocale: 'en',
    locales: ['en', 'zh-CN'],
    localeConfigs: {
      en: { label: 'English', htmlLang: 'en' },
      'zh-CN': { label: '简体中文', htmlLang: 'zh-CN' },
    },
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
          editLocalizedFiles: true,
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
          { to: '/docs/intro', label: L('Docs', '文档'), position: 'left' },
          { to: '/docs/domains/api-reference', label: 'API', position: 'left' },
          {
            href: 'https://github.com/voidvec/fulla/blob/master/benchmarks/competitors/results/COMPARISON.md',
            label: L('Benchmarks', '性能基准'),
            position: 'left',
          },
          { to: '/docs/adr/ADR-0001', label: 'ADR', position: 'left' },
          {
            type: 'localeDropdown',
            position: 'right',
          },
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
            title: L('Docs', '文档'),
            items: [
              { label: L('Get Started', '开始'), to: '/docs/intro' },
              { label: L('Architecture', '架构总览'), to: '/docs/architecture/architecture-overview' },
              { label: L('API Reference', 'API 参考'), to: '/docs/domains/api-reference' },
              { label: L('SDK Integration', 'SDK 集成'), to: '/docs/sdk/sdk-integration-guide' },
            ],
          },
          {
            title: L('Resources', '资源'),
            items: [
              { label: L('Production Deployment', '生产部署'), to: '/docs/operate/deployment' },
              { label: L('Benchmarks', '性能基准'), href: 'https://github.com/voidvec/fulla/blob/master/benchmarks/competitors/results/COMPARISON.md' },
              { label: L('OpenAPI Contract', 'OpenAPI 契约'), href: 'https://github.com/voidvec/fulla/blob/master/apps/server/openapi.yaml' },
              { label: L('ADRs', '架构决策记录'), to: '/docs/adr/ADR-0001' },
            ],
          },
          {
            title: L('Community', '社区'),
            items: [
              { label: 'GitHub', href: 'https://github.com/voidvec/fulla' },
              { label: L('Chinese Wiki', '中文 Wiki'), href: 'https://github.com/voidvec/fulla/wiki' },
              { label: L('Issues', '问题反馈'), href: 'https://github.com/voidvec/fulla/issues' },
            ],
          },
        ],
          copyright: L(
            'Copyright © 2026 Luca · MIT License · Site content from the repo’s docs tree (single source of truth)',
            'Copyright © 2026 Luca · MIT License · 本站内容来自仓库 docs 目录（单一事实源）',
          ),
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
