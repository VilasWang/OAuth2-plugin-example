import React from 'react';
import clsx from 'clsx';
import Layout from '@theme/Layout';

import styles from './index.module.css';

export default function Home() {
  return (
    <Layout
      title="Fulla — High-performance open-source IAM core"
      description="Production-grade OAuth2/OIDC authorization server and embeddable C++17 SDK, with admin console, user frontend and multi-language clients.">
      <main className={clsx('hero', styles.heroBanner)}>
        <div className="container">
          <h1 className="hero__title">Fulla</h1>
          <p className="hero__subtitle">
            High-performance open-source IAM core — C++17
          </p>
          <p>
            Production-grade OAuth2/OIDC authorization server as a ready-to-run
            product (Docker/Helm) or an embeddable SDK (<code>find_package(fulla-*)</code>),
            with typed Python/Go clients. All five benchmark scenarios lead
            Keycloak, Ory Hydra and Zitadel.
          </p>
          <div className={styles.buttons}>
            <a className="button button--primary button--lg" href="/docs/intro">
              Read the Docs
            </a>
            <a
              className="button button--secondary button--lg"
              href="https://github.com/voidvec/fulla">
              GitHub
            </a>
          </div>
        </div>
      </main>
    </Layout>
  );
}
