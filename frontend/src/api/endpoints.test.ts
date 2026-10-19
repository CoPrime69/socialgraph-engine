import { beforeEach, describe, expect, it } from 'vitest';
import { sessionStore } from '../auth/sessionStore';
import { authResponse, jonSkeet } from '../test/fixtures';
import { backend } from '../test/mockBackend';
import { analyticsApi, authApi, contentApi, graphApi } from './endpoints';
import { graphVersionStore } from './graphVersion';

beforeEach(() => {
  sessionStore.start(authResponse);
});

describe('analytics requests (read-your-writes)', () => {
  it('omits min_version until a version is known, then records the version seen', async () => {
    const summary = await analyticsApi.summary();
    expect(backend.lastRequest('/api/v1/analytics/summary')?.query.has('min_version')).toBe(false);
    expect(summary.graph_version).toBe(41);
    expect(summary.stale).toBe(false);
    expect(graphVersionStore.get()).toBe(41);

    await analyticsApi.summary();
    expect(backend.lastRequest('/api/v1/analytics/summary')?.query.get('min_version')).toBe('41');
  });

  it('sends the version returned by a connection change and exposes X-Graph-Stale', async () => {
    backend.state.analyticsPaused = true;
    const created = await graphApi.connect(jonSkeet.id);
    expect(created.graph_version).toBe(42);
    graphVersionStore.observe(created.graph_version);

    const recommendations = await analyticsApi.recommendations(10);
    expect(backend.lastRequest('/api/v1/me/recommendations')?.query.get('min_version')).toBe('42');
    expect(recommendations.stale).toBe(true);
    expect(recommendations.graph_version).toBe(41);

    backend.resumeAnalytics();
    const fresh = await analyticsApi.recommendations(10);
    expect(fresh.stale).toBe(false);
    expect(fresh.graph_version).toBe(42);
  });

  it('sends the graph scope: top-N or an ego network', async () => {
    await analyticsApi.graph({ limit: 500 });
    let query = backend.lastRequest('/api/v1/analytics/graph')?.query;
    expect(query?.get('limit')).toBe('500');
    expect(query?.has('focus')).toBe(false);
    expect(query?.has('depth')).toBe(false);

    await analyticsApi.graph({ limit: 150, focus: jonSkeet.id, depth: 2 });
    query = backend.lastRequest('/api/v1/analytics/graph')?.query;
    expect(query?.get('focus')).toBe(String(jonSkeet.id));
    expect(query?.get('depth')).toBe('2');
  });

  it('asks for a bounded number of members per community', async () => {
    await analyticsApi.communities();
    expect(backend.lastRequest('/api/v1/analytics/communities')?.query.get('member_limit')).toBe(
      '24',
    );
  });
});

describe('graph and content requests', () => {
  it('passes list filters through as query parameters', async () => {
    await graphApi.users({ q: 'skeet', sort: 'name', min_reputation: 1000, limit: 20, offset: 40 });
    const query = backend.lastRequest('/api/v1/users')?.query;
    expect(Object.fromEntries(query ?? [])).toEqual({
      q: 'skeet',
      sort: 'name',
      min_reputation: '1000',
      limit: '20',
      offset: '40',
    });

    await contentApi.questions({ tag: 'c#', sort: 'recent', limit: 20, offset: 0 });
    expect(backend.lastRequest('/api/v1/questions')?.query.get('tag')).toBe('c#');
  });

  it('fetches the taxonomy and signs in without a bearer token', async () => {
    await graphApi.taxonomy();
    expect(backend.lastRequest('/api/v1/meta/taxonomy')?.token).toBeNull();
    sessionStore.end('signed_out');
    const auth = await authApi.login({ handle: 'ada_member', password: 'correct-horse-battery' });
    expect(auth.user.handle).toBe('ada_member');
    expect(backend.lastRequest('/api/v1/auth/login', 'POST')?.token).toBeNull();
  });
});
