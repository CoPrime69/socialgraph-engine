/** CC BY-SA 4.0 requires attribution wherever the Stack Overflow data is shown. */
export function DatasetAttribution({ className }: { className?: string }) {
  return (
    <p className={className}>
      Network data: Stack Overflow, Aug-Dec 2008 (Stack Exchange Data Dump), licensed{' '}
      <a
        href="https://creativecommons.org/licenses/by-sa/4.0/"
        rel="noopener noreferrer"
        target="_blank"
      >
        CC BY-SA 4.0
      </a>
      .
    </p>
  );
}
