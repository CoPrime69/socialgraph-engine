import { describe, expect, it } from 'vitest';
import {
  createDevProxy,
  DEFAULT_UPSTREAMS,
  resolveProxyTarget,
  resolveUpstreams,
} from './devProxy';

const proxy = createDevProxy(DEFAULT_UPSTREAMS);
const target = (url: string) => resolveProxyTarget(proxy, url);

describe('dev proxy routing (contracts §2 and §10.6)', () => {
  it.each([
    ['/api/v1/auth/login', DEFAULT_UPSTREAMS.identity],
    ['/api/v1/auth/register', DEFAULT_UPSTREAMS.identity],
    ['/api/v1/meta/taxonomy', DEFAULT_UPSTREAMS.graph],
    ['/api/v1/users?q=jon&limit=20', DEFAULT_UPSTREAMS.graph],
    ['/api/v1/users/22656', DEFAULT_UPSTREAMS.graph],
    ['/api/v1/me/profile', DEFAULT_UPSTREAMS.graph],
    ['/api/v1/me/connections/22656', DEFAULT_UPSTREAMS.graph],
    ['/api/v1/users/22656/metrics?min_version=3', DEFAULT_UPSTREAMS.analytics],
    ['/api/v1/analytics/graph?limit=1500', DEFAULT_UPSTREAMS.analytics],
    ['/api/v1/paths?to=22656', DEFAULT_UPSTREAMS.analytics],
    ['/api/v1/me/recommendations?limit=10', DEFAULT_UPSTREAMS.analytics],
    ['/api/v1/users/22656/activity', DEFAULT_UPSTREAMS.content],
    ['/api/v1/tags?q=py', DEFAULT_UPSTREAMS.content],
    ['/api/v1/questions/7074', DEFAULT_UPSTREAMS.content],
  ])('sends %s to %s', (url, upstream) => {
    expect(target(url)).toBe(upstream);
  });

  it('does not match look-alike paths', () => {
    expect(target('/api/v1/usersettings')).toBeUndefined();
    expect(target('/api/v1/users/22656/metrics/extra')).toBe(DEFAULT_UPSTREAMS.graph);
    expect(target('/index.html')).toBeUndefined();
  });

  it('reads upstream overrides from the environment', () => {
    expect(
      resolveUpstreams({
        VITE_IDENTITY_URL: ' http://identity:9000/ ',
        VITE_CONTENT_URL: '',
      }),
    ).toEqual({ ...DEFAULT_UPSTREAMS, identity: 'http://identity:9000' });
  });

  it('forwards with the upstream as origin', () => {
    expect(Object.values(proxy).every((rule) => rule.changeOrigin && rule.xfwd)).toBe(true);
  });
});
