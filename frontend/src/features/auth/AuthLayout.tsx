import type { ReactNode } from 'react';
import { DatasetAttribution } from '../../layout/DatasetAttribution';
import { Logo } from '../../layout/Logo';
import styles from './AuthLayout.module.css';

interface AuthLayoutProps {
  title: string;
  subtitle: ReactNode;
  children: ReactNode;
  footer: ReactNode;
}

export function AuthLayout({ title, subtitle, children, footer }: AuthLayoutProps) {
  return (
    <div className={styles.layout}>
      <aside className={styles.intro} aria-label="About SocialGraph">
        <div className={styles.brand}>
          <Logo size={36} />
          <span className={styles.brandName}>SocialGraph</span>
        </div>
        <p className={styles.tagline}>
          See who drives the conversation in a real developer community, and who you should meet
          next.
        </p>
        <ul className={styles.points}>
          <li>Rank influencers by PageRank, betweenness, closeness or degree.</li>
          <li>See the communities the network naturally splits into.</li>
          <li>Get connection recommendations with a plain-language explanation.</li>
          <li>Find the shortest chain of people between you and anyone else.</li>
        </ul>
        <DatasetAttribution className={styles.attribution} />
      </aside>
      <main className={styles.main} id="main">
        <div className={styles.card}>
          <div className={styles.mobileBrand} aria-hidden="true">
            <Logo size={32} />
            <span className={styles.brandName}>SocialGraph</span>
          </div>
          <h1 className={styles.title}>{title}</h1>
          <p className={styles.subtitle}>{subtitle}</p>
          {children}
          <p className={styles.footer}>{footer}</p>
        </div>
        <DatasetAttribution className={styles.mobileAttribution} />
      </main>
    </div>
  );
}
