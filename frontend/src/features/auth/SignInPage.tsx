import { useMutation } from '@tanstack/react-query';
import { useRef, useState, type FormEvent } from 'react';
import { Link } from 'react-router';
import { authApi } from '../../api/endpoints';
import { describeError, isApiError, type ApiError } from '../../api/errors';
import { useAuth } from '../../auth/authContext';
import { sessionStore, type SignOutReason } from '../../auth/sessionStore';
import { Button } from '../../components/Button';
import { TextField } from '../../components/Field';
import { useCountdown } from '../../hooks/useCountdown';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { AuthLayout } from './AuthLayout';
import { FormAlert } from './FormAlert';
import { PasswordToggle } from './PasswordToggle';
import {
  mapServerErrors,
  normalizeHandle,
  validateSignIn,
  type FieldErrors,
  type SignInField,
} from './validation';
import styles from './AuthForm.module.css';

const NOTICES: Record<SignOutReason, string> = {
  expired: 'Your session expired. Please sign in again.',
  unauthorized: 'Your session is no longer valid. Please sign in again.',
  signed_out: 'You have been signed out.',
};

export function SignInPage() {
  useDocumentTitle('Sign in');
  const { signIn } = useAuth();
  const [notice] = useState(() => {
    const reason = sessionStore.getLastSignOutReason();
    return reason ? NOTICES[reason] : null;
  });
  const [handle, setHandle] = useState('');
  const [password, setPassword] = useState('');
  const [showPassword, setShowPassword] = useState(false);
  const [errors, setErrors] = useState<FieldErrors<SignInField>>({});
  const [formError, setFormError] = useState<string | null>(null);
  const countdown = useCountdown();
  const handleRef = useRef<HTMLInputElement>(null);
  const passwordRef = useRef<HTMLInputElement>(null);

  const focusField = (field: SignInField | undefined) => {
    if (field === 'handle') handleRef.current?.focus();
    else if (field === 'password') passwordRef.current?.focus();
  };

  const login = useMutation({
    mutationFn: authApi.login,
    onSuccess: (auth) => {
      // <RedirectIfAuthenticated> navigates to the page the user originally wanted.
      signIn(auth);
    },
    onError: (error) => {
      handleError(error);
    },
  });

  function handleError(error: Error) {
    if (!isApiError(error)) {
      setFormError(describeError(error).message);
      return;
    }
    const apiError: ApiError = error;
    if (apiError.code === 'invalid_credentials' || apiError.status === 401) {
      setFormError('Incorrect handle or password.');
      setPassword('');
      focusField('password');
      return;
    }
    if (apiError.code === 'rate_limited') {
      setFormError(describeError(apiError).message);
      if (apiError.retryAfterSeconds !== null) countdown.start(apiError.retryAfterSeconds);
      return;
    }
    if (apiError.code === 'validation_failed' && apiError.details.length > 0) {
      const mapped = mapServerErrors(apiError.details, ['handle', 'password'] as const);
      setErrors(mapped.fieldErrors);
      setFormError(mapped.other.length > 0 ? mapped.other.join(' ') : null);
      focusField(
        mapped.fieldErrors.handle ? 'handle' : mapped.fieldErrors.password ? 'password' : undefined,
      );
      return;
    }
    const description = describeError(apiError);
    setFormError(`${description.title}. ${description.message}`);
  }

  const onSubmit = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    setFormError(null);
    const nextErrors = validateSignIn({ handle, password });
    setErrors(nextErrors);
    if (nextErrors.handle || nextErrors.password) {
      focusField(nextErrors.handle ? 'handle' : 'password');
      return;
    }
    login.mutate({ handle: normalizeHandle(handle), password });
  };

  const locked = countdown.remaining > 0;

  return (
    <AuthLayout
      title="Sign in"
      subtitle="Welcome back. Sign in with your SocialGraph handle."
      footer={
        <>
          New here? <Link to="/register">Create an account</Link>
        </>
      }
    >
      {notice ? <FormAlert tone="info">{notice}</FormAlert> : null}
      <form
        className={styles.form}
        onSubmit={onSubmit}
        noValidate
        aria-describedby={formError ? 'signin-error' : undefined}
      >
        {formError ? (
          <FormAlert tone="error" id="signin-error">
            {formError}
          </FormAlert>
        ) : null}
        <TextField
          ref={handleRef}
          label="Handle"
          name="handle"
          autoComplete="username"
          autoCapitalize="none"
          autoCorrect="off"
          spellCheck={false}
          value={handle}
          onChange={(event) => {
            setHandle(event.target.value);
          }}
          error={errors.handle}
          required
        />
        <TextField
          ref={passwordRef}
          label="Password"
          name="password"
          type={showPassword ? 'text' : 'password'}
          autoComplete="current-password"
          value={password}
          onChange={(event) => {
            setPassword(event.target.value);
          }}
          error={errors.password}
          required
          trailing={
            <PasswordToggle
              visible={showPassword}
              onToggle={() => {
                setShowPassword((visible) => !visible);
              }}
            />
          }
        />
        <Button
          type="submit"
          variant="primary"
          className={styles.submit}
          loading={login.isPending}
          loadingLabel="Signing in…"
          disabled={locked}
        >
          {locked ? `Try again in ${countdown.remaining}s` : 'Sign in'}
        </Button>
      </form>
    </AuthLayout>
  );
}
