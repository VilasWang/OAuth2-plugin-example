// fulla.dev — Docusaurus site.
// Core decision (docs/documentation-governance.md §四): the repo's docs/ tree
// IS the site source (docs.path below points one level up). No content copies,
// no sync scripts — a drifted duplicate is exactly what the governance kills.
// Local-only process docs live in docs-local/ (outside this tree, gitignored).

const lightCodeTheme = require('prism-react-renderer').themes.github;
const darkCodeTheme = require('prism-react-renderer').themes.dracula;

/** @type {import('@docusaurus/types').Config} */
const config = {
  title: 'Fulla',
  tagline: 'High-performance open-source IAM core (C++17)',
  url: 'https://fulla.dev',
  baseUrl: '/',
  onBrokenLinks: 'throw',
  favicon: 'img/favicon.svg',
  organizationName: 'voidvec',
  projectName: 'fulla',

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

  markdown: {
    mermaid: true,
  },
  themes: ['@docusaurus/theme-mermaid'],

  themeConfig:
    /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
    ({
      navbar: {
        title: 'Fulla',
        logo: { alt: 'Fulla', src: 'img/favicon.svg' },
        items: [
          { to: '/docs/intro', label: 'Docs', position: 'left' },
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
          { label: 'Docs', to: '/docs/intro' },
          { label: 'Architecture', to: '/docs/architecture/architecture-overview' },
          { label: 'ADRs', to: '/docs/adr/ADR-0001' },
          { label: 'GitHub', href: 'https://github.com/voidvec/fulla' },
          { label: 'Wiki (zh)', href: 'https://github.com/voidvec/fulla/wiki' },
        ],
        copyright: `Copyright © 2026 Luca — MIT License`,
      },
      prism: {
        theme: lightCodeTheme,
        darkTheme: darkCodeTheme,
      },
    }),
};

module.exports = config;
