import { lazy, Suspense } from 'react';
import { Outlet, type RouteObject } from 'react-router';
import { RedirectIfAuthenticated, RequireAuth } from '../auth/guards';
import { Spinner } from '../components/Spinner';
import { NotFoundPage } from '../features/errors/NotFoundPage';
import { RouteErrorPage } from '../features/errors/RouteErrorPage';
import { AppShell } from '../layout/AppShell';

// Every page is its own chunk; the shell, providers and API client are in the entry chunk.
const SignInPage = lazy(() =>
  import('../features/auth/SignInPage').then((m) => ({ default: m.SignInPage })),
);
const RegisterPage = lazy(() =>
  import('../features/auth/RegisterPage').then((m) => ({ default: m.RegisterPage })),
);
const OverviewPage = lazy(() =>
  import('../features/overview/OverviewPage').then((m) => ({ default: m.OverviewPage })),
);
const MyNetworkPage = lazy(() =>
  import('../features/network/MyNetworkPage').then((m) => ({ default: m.MyNetworkPage })),
);
const CommunitiesPage = lazy(() =>
  import('../features/communities/CommunitiesPage').then((m) => ({
    default: m.CommunitiesPage,
  })),
);
const PeoplePage = lazy(() =>
  import('../features/people/PeoplePage').then((m) => ({ default: m.PeoplePage })),
);
const UserDetailRoute = lazy(() =>
  import('../features/users/UserDetailPage').then((m) => ({ default: m.UserDetailRoute })),
);
const TopicsPage = lazy(() =>
  import('../features/topics/TopicsPage').then((m) => ({ default: m.TopicsPage })),
);
const QuestionRoute = lazy(() =>
  import('../features/topics/QuestionPage').then((m) => ({ default: m.QuestionRoute })),
);

function RootLayout() {
  return (
    <Suspense
      fallback={
        <div className="app-loading">
          <Spinner label="Loading SocialGraph…" />
        </div>
      }
    >
      <Outlet />
    </Suspense>
  );
}

export const appRoutes: RouteObject[] = [
  {
    element: <RootLayout />,
    errorElement: <RouteErrorPage />,
    children: [
      {
        path: '/sign-in',
        element: (
          <RedirectIfAuthenticated>
            <SignInPage />
          </RedirectIfAuthenticated>
        ),
      },
      {
        path: '/register',
        element: (
          <RedirectIfAuthenticated>
            <RegisterPage />
          </RedirectIfAuthenticated>
        ),
      },
      {
        element: (
          <RequireAuth>
            <AppShell />
          </RequireAuth>
        ),
        children: [
          {
            errorElement: <RouteErrorPage inShell />,
            children: [
              { index: true, element: <OverviewPage /> },
              { path: 'network', element: <MyNetworkPage /> },
              { path: 'communities', element: <CommunitiesPage /> },
              { path: 'people', element: <PeoplePage /> },
              { path: 'users/:userId', element: <UserDetailRoute /> },
              { path: 'topics', element: <TopicsPage /> },
              { path: 'questions/:questionId', element: <QuestionRoute /> },
              { path: '*', element: <NotFoundPage /> },
            ],
          },
        ],
      },
    ],
  },
];
