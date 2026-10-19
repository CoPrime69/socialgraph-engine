import { screen, waitFor, within } from '@testing-library/react';
import { HttpResponse } from 'msw';
import { describe, expect, it, vi } from 'vitest';
import { axeViolations } from '../../test/axe';
import { questions } from '../../test/fixtures';
import { backend, errorResponse, route } from '../../test/mockBackend';
import { renderApp } from '../../test/render';
import { server } from '../../test/server';

const QUESTIONS = '/api/v1/questions';
const TAGS = '/api/v1/tags';
// Titles of the fixture questions, highest score first.
const stringVsString = 'What is the difference between String and string in C#?';
const enumerate = 'How to enumerate an enum?';
const castToEnum = 'How do I cast int to enum in C#?';

async function openTopics(path = '/topics') {
  const view = renderApp(path);
  await screen.findByRole('heading', { level: 1, name: 'Topics' });
  const tagCard = screen.getByRole('region', { name: 'Tags' });
  await within(tagCard).findByRole('link', { name: 'c# 7,456 questions' });
  await waitFor(() => {
    expect(titles()).not.toHaveLength(0);
  });
  return { ...view, tagCard };
}

/** The questions card: "All questions" or "Questions tagged “…”". */
function questionCard(): HTMLElement {
  return screen.getByRole('region', { name: /questions/i });
}

function titles(): string[] {
  return Array.from(
    questionCard().querySelectorAll('ol > li a[href^="/questions/"]'),
    (link) => link.textContent,
  );
}

function questionItem(title: string): HTMLElement {
  const item = within(questionCard()).getByRole('link', { name: title }).closest('li');
  if (!item) throw new Error(`No list item for ${title}`);
  return item;
}

function sentQueries(path: string, key: string): (string | null)[] {
  return backend.requests(path).map((request) => request.query.get(key));
}

describe('Topics', () => {
  it('lists tags and the top-scored questions', async () => {
    const { tagCard } = await openTopics();
    expect(
      within(tagCard)
        .getAllByRole('link')
        .map((link) => link.getAttribute('href')),
    ).toEqual([
      '/topics?tag=c%23',
      '/topics?tag=.net',
      '/topics?tag=java',
      '/topics?tag=asp.net',
      '/topics?tag=c%2B%2B',
      '/topics?tag=javascript',
    ]);
    expect(backend.lastRequest(TAGS)?.query.get('limit')).toBe('40');

    expect(
      within(questionCard()).getByRole('heading', { name: 'All questions' }),
    ).toBeInTheDocument();
    expect(within(questionCard()).getByText('3 questions')).toBeInTheDocument();
    expect(titles()).toEqual([stringVsString, enumerate, castToEnum]);
    const request = backend.lastRequest(QUESTIONS);
    expect(request?.query.get('sort')).toBe('score');
    expect(request?.query.get('limit')).toBe('20');
    expect(request?.query.get('offset')).toBe('0');
    expect(request?.query.has('tag')).toBe(false);

    const first = questionItem(stringVsString);
    expect(first).toHaveTextContent('7,529 score');
    expect(first).toHaveTextContent('68 answers');
    expect(first).toHaveTextContent('Asked Aug 10, 2008 by Lance Fisher · 1,308,690 views');
    expect(within(first).getByRole('link', { name: 'Lance Fisher' })).toHaveAttribute(
      'href',
      '/users/571',
    );
    expect(
      within(within(first).getByRole('list', { name: 'Tags' }))
        .getAllByRole('link')
        .map((link) => link.textContent),
    ).toEqual(['c#', '.net', 'string', 'types', 'alias']);
    expect(within(first).getByRole('link', { name: stringVsString })).toHaveAttribute(
      'href',
      '/questions/7074',
    );

    // A question whose author account is gone has no "by".
    expect(questionItem(castToEnum)).toHaveTextContent('Asked Aug 27, 2008 · 2,044,744 views');
  });

  it('shows the questions of a tag and clears it again', async () => {
    const { user, router, tagCard } = await openTopics();
    const csharp = within(tagCard).getByRole('link', { name: 'c# 7,456 questions' });
    await user.click(csharp);

    expect(router.state.location.search).toBe('?tag=c%23');
    expect(
      await within(questionCard()).findByRole('heading', { name: 'Questions tagged “c#”' }),
    ).toBeInTheDocument();
    expect(csharp).toHaveAttribute('aria-current', 'true');
    await waitFor(() => {
      expect(backend.lastRequest(QUESTIONS)?.query.get('tag')).toBe('c#');
    });
    expect(document.title).toBe('Questions tagged c# · SocialGraph');

    await user.click(within(tagCard).getByRole('link', { name: 'Clear tag: c#' }));
    expect(router.state.location.search).toBe('');
    expect(
      await within(questionCard()).findByRole('heading', { name: 'All questions' }),
    ).toBeInTheDocument();
    expect(document.title).toBe('Topics · SocialGraph');
  });

  it('opens a tag from a question’s tag chips', async () => {
    const { user, router } = await openTopics();
    const tags = within(questionItem(enumerate)).getByRole('list', { name: 'Tags' });
    await user.click(within(tags).getByRole('link', { name: 'enums' }));
    expect(router.state.location.search).toBe('?tag=enums');
    await waitFor(() => {
      expect(titles()).toEqual([enumerate, castToEnum]);
    });
  });

  it('explains a tag without questions in the sample', async () => {
    renderApp('/topics?tag=java');
    expect(await screen.findByText('No questions found')).toBeInTheDocument();
    expect(
      screen.getByText('There are no questions with this tag in the sample.'),
    ).toBeInTheDocument();
  });

  it('finds tags by prefix after a pause in typing', async () => {
    const { user, tagCard } = await openTopics();
    await user.type(within(tagCard).getByRole('searchbox', { name: 'Find a tag' }), 'ja');
    await waitFor(() => {
      expect(
        within(tagCard)
          .getAllByRole('link')
          .map((link) => link.textContent),
      ).toEqual(['java 4,278 questions', 'javascript 2,724 questions']);
    });
    expect(sentQueries(TAGS, 'q').filter((q) => q !== null)).toEqual(['ja']);

    await user.type(within(tagCard).getByRole('searchbox', { name: 'Find a tag' }), 'zz');
    expect(await within(tagCard).findByText('No tags match')).toBeInTheDocument();
  });

  it('searches question titles after a pause in typing', async () => {
    const { user, router } = await openTopics();
    const search = screen.getByRole('searchbox', { name: 'Search titles' });
    await user.type(search, 'enum');
    await waitFor(() => {
      expect(router.state.location.search).toBe('?q=enum');
    });
    await waitFor(() => {
      expect(titles()).toEqual([enumerate, castToEnum]);
    });
    expect(sentQueries(QUESTIONS, 'q').filter((q) => q !== null)).toEqual(['enum']);

    await user.clear(search);
    await user.type(search, 'zzz');
    expect(await screen.findByText('No questions found')).toBeInTheDocument();
    expect(screen.getByText('Try different words or another tag.')).toBeInTheDocument();
    expect(search).toHaveValue('zzz');
  });

  it('sorts by newest and keeps the order when choosing a tag', async () => {
    const { user, router, tagCard } = await openTopics();
    await user.click(screen.getByRole('radio', { name: 'Newest' }));
    expect(router.state.location.search).toBe('?sort=recent');
    await waitFor(() => {
      expect(titles()).toEqual([enumerate, castToEnum, stringVsString]);
    });
    expect(backend.lastRequest(QUESTIONS)?.query.get('sort')).toBe('recent');
    expect(within(tagCard).getByRole('link', { name: 'c# 7,456 questions' })).toHaveAttribute(
      'href',
      '/topics?tag=c%23&sort=recent',
    );

    await user.click(screen.getByRole('radio', { name: 'Top scored' }));
    expect(router.state.location.search).toBe('');
  });

  it('pages through questions', async () => {
    server.use(
      route('get', `*${QUESTIONS}`, ({ url }) => {
        const offset = Number(url.searchParams.get('offset') ?? '0');
        const items = offset === 0 ? questions : questions.slice(2);
        return HttpResponse.json({ items, total: 45, limit: 20, offset });
      }),
    );
    const scrollTo = vi.spyOn(window, 'scrollTo');
    const { user, router } = await openTopics();
    const pagination = within(questionCard()).getByRole('navigation', { name: 'Pagination' });
    expect(pagination).toHaveTextContent('Showing 1–20 of 45 questions');

    await user.click(within(pagination).getByRole('button', { name: 'Next' }));
    expect(router.state.location.search).toBe('?page=2');
    await waitFor(() => {
      expect(titles()).toEqual([castToEnum]);
    });
    expect(backend.lastRequest(QUESTIONS)?.query.get('offset')).toBe('20');
    expect(scrollTo).toHaveBeenCalledWith({ top: 0 });

    // Changing the sort starts again from the first page.
    await user.click(screen.getByRole('radio', { name: 'Newest' }));
    expect(router.state.location.search).toBe('?sort=recent');
  });

  it('restores the tag, search, sort and page from the URL', async () => {
    renderApp('/topics?tag=c%23&q=enum&sort=recent&page=2');
    await screen.findByRole('heading', { level: 1, name: 'Topics' });
    expect(screen.getByRole('searchbox', { name: 'Search titles' })).toHaveValue('enum');
    expect(screen.getByRole('radio', { name: 'Newest' })).toBeChecked();
    await waitFor(() => {
      expect(backend.lastRequest(QUESTIONS)).toBeDefined();
    });
    const query = backend.lastRequest(QUESTIONS)?.query;
    expect(query?.get('tag')).toBe('c#');
    expect(query?.get('q')).toBe('enum');
    expect(query?.get('sort')).toBe('recent');
    expect(query?.get('offset')).toBe('20');
  });

  it('shows errors with a retry for the questions and the tags', async () => {
    let fail = true;
    server.use(
      route('get', `*${QUESTIONS}`, () =>
        fail ? errorResponse(500, 'internal', 'Content database unavailable') : undefined,
      ),
      route('get', `*${TAGS}`, () =>
        fail ? errorResponse(503, 'unavailable', 'Content service starting') : undefined,
      ),
    );
    const { user } = renderApp('/topics');
    const tagCard = await screen.findByRole('region', { name: 'Tags' });
    expect(await within(tagCard).findByText('Service temporarily unavailable')).toBeInTheDocument();
    expect(
      await within(questionCard()).findByText('Something went wrong on our side'),
    ).toBeInTheDocument();

    fail = false;
    await user.click(within(questionCard()).getByRole('button', { name: 'Try again' }));
    await waitFor(() => {
      expect(titles()).toHaveLength(3);
    });
    await user.click(within(tagCard).getByRole('button', { name: 'Try again' }));
    expect(
      await within(tagCard).findByRole('link', { name: 'c# 7,456 questions' }),
    ).toBeInTheDocument();
  });

  it('has no detectable accessibility violations', async () => {
    const { container } = await openTopics('/topics?tag=c%23');
    expect(await axeViolations(container)).toEqual([]);
  });
});
