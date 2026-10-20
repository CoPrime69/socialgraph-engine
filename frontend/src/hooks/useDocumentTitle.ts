import { useEffect } from 'react';

export const APP_NAME = 'SocialGraph';

/** Sets `document.title` to "<title> · SocialGraph" while the calling page is mounted. */
export function useDocumentTitle(title: string | null | undefined): void {
  useEffect(() => {
    document.title = title ? `${title} · ${APP_NAME}` : APP_NAME;
  }, [title]);
}
