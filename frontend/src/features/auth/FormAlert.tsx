import type { ReactNode } from 'react';
import { Icon } from '../../components/Icon';
import styles from './AuthForm.module.css';

interface FormAlertProps {
  tone: 'error' | 'info';
  children: ReactNode;
  id?: string;
}

/** Form-level message. Errors use role="alert" so they are announced immediately. */
export function FormAlert({ tone, children, id }: FormAlertProps) {
  return (
    <div
      id={id}
      className={`${styles.alert} ${tone === 'error' ? styles.alertError : styles.alertInfo}`}
      role={tone === 'error' ? 'alert' : 'status'}
    >
      <Icon name={tone === 'error' ? 'alert' : 'info'} />
      <div>{children}</div>
    </div>
  );
}
