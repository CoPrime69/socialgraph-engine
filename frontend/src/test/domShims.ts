/**
 * Minimal stand-ins for browser APIs that jsdom does not implement: media queries, scrolling,
 * modal dialogs and the canvas 2D context. They behave like the real APIs as far as the app
 * relies on them, and the canvas context records the calls made to it.
 */

const mediaMatches = new Map<string, boolean>();

/** Makes `window.matchMedia(query).matches` return `matches` (reset before every test). */
export function setMediaQuery(query: string, matches: boolean): void {
  mediaMatches.set(query, matches);
}

export function resetMediaQueries(): void {
  mediaMatches.clear();
}

export interface CanvasCall {
  name: string;
  args: unknown[];
}

const contexts = new WeakMap<
  HTMLCanvasElement,
  { context: CanvasRenderingContext2D; calls: CanvasCall[] }
>();

/** Drawing calls made on `canvas` since it was created or since the last `clearCanvasCalls`. */
export function canvasCalls(canvas: HTMLCanvasElement): CanvasCall[] {
  return contexts.get(canvas)?.calls ?? [];
}

export function clearCanvasCalls(canvas: HTMLCanvasElement): void {
  const entry = contexts.get(canvas);
  if (entry) entry.calls.length = 0;
}

/** A 2D context whose methods record their arguments and whose properties are plain fields. */
function createContext(canvas: HTMLCanvasElement): CanvasRenderingContext2D {
  const calls: CanvasCall[] = [];
  const fields: Record<PropertyKey, unknown> = { canvas };
  const context = new Proxy(fields, {
    get(target, key) {
      if (key in target) return target[key];
      if (typeof key === 'symbol') return undefined;
      if (key === 'measureText') {
        return (text: string) => {
          calls.push({ name: key, args: [text] });
          return { width: text.length * 7 };
        };
      }
      return (...args: unknown[]) => {
        calls.push({ name: key, args });
      };
    },
    set(target, key, value) {
      target[key] = value;
      return true;
    },
  }) as unknown as CanvasRenderingContext2D;
  contexts.set(canvas, { context, calls });
  return context;
}

export function installDomShims(): void {
  window.matchMedia = (query: string): MediaQueryList => {
    const list: Omit<MediaQueryList, 'addListener' | 'removeListener'> = {
      matches: mediaMatches.get(query) ?? false,
      media: query,
      onchange: null,
      addEventListener: () => undefined,
      removeEventListener: () => undefined,
      dispatchEvent: () => false,
    };
    // The deprecated addListener/removeListener are not used by the app.
    return list as MediaQueryList;
  };

  window.scrollTo = () => undefined;
  Element.prototype.scrollIntoView = () => undefined;

  HTMLDialogElement.prototype.showModal = function showModal(this: HTMLDialogElement) {
    this.open = true;
  };
  HTMLDialogElement.prototype.close = function close(this: HTMLDialogElement) {
    if (!this.open) return;
    this.open = false;
    this.dispatchEvent(new Event('close'));
  };

  HTMLCanvasElement.prototype.getContext = function getContext(
    this: HTMLCanvasElement,
    type: string,
  ) {
    if (type !== '2d') return null;
    return contexts.get(this)?.context ?? createContext(this);
  } as typeof HTMLCanvasElement.prototype.getContext;
}
