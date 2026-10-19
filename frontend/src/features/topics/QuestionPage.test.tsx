import { screen, within } from '@testing-library/react';
import { http, HttpResponse } from 'msw';
import { describe, expect, it } from 'vitest';
import type { QuestionDetail } from '../../api/types';
import { axeViolations } from '../../test/axe';
import { questionDetail } from '../../test/fixtures';
import { backend, errorResponse, route } from '../../test/mockBackend';
import { renderApp } from '../../test/render';
import { server } from '../../test/server';

const QUESTION = '*/api/v1/questions/:questionId';

async function openQuestion(id: number | string = questionDetail.id) {
  const view = renderApp(`/questions/${String(id)}`);
  await screen.findByRole('heading', { level: 1 });
  return view;
}

function statistic(label: string): string | null | undefined {
  const stats = screen.getByLabelText('Question statistics');
  return within(stats).getByText(label).nextElementSibling?.textContent;
}

describe('Question', () => {
  it('shows the question, its statistics and who answered', async () => {
    await openQuestion();
    expect(screen.getByRole('heading', { level: 1 })).toHaveTextContent(questionDetail.title);
    expect(document.title).toBe(`${questionDetail.title} · SocialGraph`);
    expect(screen.getByRole('link', { name: 'All topics' })).toHaveAttribute('href', '/topics');
    expect(screen.getByRole('link', { name: 'Lance Fisher' })).toHaveAttribute(
      'href',
      '/users/571',
    );

    const external = screen.getByRole('link', {
      name: 'Read on Stack Overflow (opens in a new tab)',
    });
    expect(external).toHaveAttribute('href', 'https://stackoverflow.com/q/7074');
    expect(external).toHaveAttribute('target', '_blank');
    expect(external).toHaveAttribute('rel', 'noopener noreferrer');

    expect(
      within(screen.getByRole('list', { name: 'Tags' }))
        .getAllByRole('link')
        .map((link) => link.getAttribute('href')),
    ).toEqual([
      '/topics?tag=c%23',
      '/topics?tag=.net',
      '/topics?tag=string',
      '/topics?tag=types',
      '/topics?tag=alias',
    ]);

    expect(statistic('Score')).toBe('7,529');
    expect(statistic('Answers')).toBe('68');
    expect(statistic('Views')).toBe('1,308,690');
    expect(statistic('Comments')).toBe('16');

    const answers = within(screen.getByRole('region', { name: 'Answers' })).getAllByRole(
      'listitem',
    );
    expect(answers).toHaveLength(3);
    const [accepted, skeet, unlinked] = answers as [HTMLElement, HTMLElement, HTMLElement];
    expect(accepted).toHaveTextContent('7,062 score');
    expect(within(accepted).getByRole('link', { name: 'Derek Park' })).toHaveAttribute(
      'href',
      '/users/872',
    );
    expect(within(accepted).getByText('Accepted')).toBeInTheDocument();
    expect(accepted).toHaveTextContent('Answered Aug 10, 2008 · View answer');
    expect(
      within(accepted).getByRole('link', {
        name: 'View answer on Stack Overflow (opens in a new tab)',
      }),
    ).toHaveAttribute('href', 'https://stackoverflow.com/a/7077');

    expect(within(skeet).queryByText('Accepted')).not.toBeInTheDocument();
    const skeetLink = within(skeet).getByRole('link', { name: /View answer/ });
    expect(skeetLink).toHaveAttribute('rel', 'noopener noreferrer');
    expect(skeetLink).toHaveAttribute('target', '_blank');

    // An answer without a link in the dataset shows no "View answer".
    expect(within(unlinked).queryByRole('link', { name: /View answer/ })).not.toBeInTheDocument();
    expect(unlinked).toHaveTextContent('Answered Oct 18, 2008');
  });

  it('marks closed questions and never links to non-https addresses', async () => {
    const closed: QuestionDetail = {
      ...questionDetail,
      owner: null,
      closed_at: '2008-12-01T10:00:00Z',
      url: 'http://stackoverflow.com/q/7074',
      answers: [
        {
          id: 1,
          owner: null,
          score: -2,
          created_at: '2008-08-11T09:00:00Z',
          is_accepted: false,
          url: 'javascript:alert(1)',
        },
      ],
    };
    server.use(http.get(QUESTION, () => HttpResponse.json(closed)));
    await openQuestion();
    expect(screen.getByText('Closed Dec 1, 2008')).toBeInTheDocument();
    expect(screen.queryByRole('link', { name: /Read on Stack Overflow/ })).not.toBeInTheDocument();
    expect(screen.queryByText(/ by /)).not.toBeInTheDocument();

    const answers = screen.getByRole('region', { name: 'Answers' });
    expect(within(answers).getByText('Unknown author')).toBeInTheDocument();
    expect(within(answers).getByText('-2')).toBeInTheDocument();
    expect(within(answers).queryByRole('link')).not.toBeInTheDocument();
  });

  it('says when no answers are in the sample', async () => {
    await openQuestion(105372);
    expect(screen.getByRole('heading', { level: 1 })).toHaveTextContent(
      'How to enumerate an enum?',
    );
    expect(screen.getByText('No answers in the sample')).toBeInTheDocument();
  });

  it('shows "Question not found" for questions outside the sample', async () => {
    await openQuestion(1);
    expect(screen.getByRole('heading', { level: 1 })).toHaveTextContent('Question not found');
    expect(screen.getByText('This question is not part of the 2008 sample.')).toBeInTheDocument();
    expect(screen.getByRole('link', { name: 'Go to the overview' })).toHaveAttribute('href', '/');
  });

  it('rejects malformed question addresses without a request', async () => {
    await openQuestion('7074abc');
    expect(screen.getByRole('heading', { level: 1 })).toHaveTextContent('Question not found');
    expect(backend.requests('/api/v1/questions/7074abc')).toHaveLength(0);
  });

  it('offers a retry when the question cannot be loaded', async () => {
    let fail = true;
    server.use(
      route('get', QUESTION, () => (fail ? errorResponse(500, 'internal', 'boom') : undefined)),
    );
    const { user } = renderApp(`/questions/${String(questionDetail.id)}`);
    expect(await screen.findByText("We couldn't load this question")).toBeInTheDocument();
    fail = false;
    await user.click(screen.getByRole('button', { name: 'Try again' }));
    expect(await screen.findByRole('heading', { level: 1 })).toHaveTextContent(
      questionDetail.title,
    );
  });

  it('opens from the list of questions', async () => {
    const { user } = renderApp('/topics');
    await user.click(await screen.findByRole('link', { name: questionDetail.title }));
    expect(
      await screen.findByRole('heading', { level: 1, name: questionDetail.title }),
    ).toBeInTheDocument();
    expect(screen.getByRole('main')).toHaveFocus();
  });

  it('has no detectable accessibility violations', async () => {
    const { container } = await openQuestion();
    expect(await axeViolations(container)).toEqual([]);
  });
});
