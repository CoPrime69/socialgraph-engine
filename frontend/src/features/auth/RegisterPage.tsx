import { useMutation } from '@tanstack/react-query';
import { useRef, useState, type FormEvent } from 'react';
import { Link } from 'react-router';
import { authApi } from '../../api/endpoints';
import { describeError, isApiError } from '../../api/errors';
import { useTaxonomy } from '../../api/queries';
import { useAuth } from '../../auth/authContext';
import { BranchOptions } from '../../components/BranchOptions';
import { Button } from '../../components/Button';
import { SelectField, TextAreaField, TextField } from '../../components/Field';
import { ErrorState } from '../../components/StatusViews';
import { useCountdown } from '../../hooks/useCountdown';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { AuthLayout } from './AuthLayout';
import { FormAlert } from './FormAlert';
import { PasswordToggle } from './PasswordToggle';
import {
  ABOUT_MAX_CHARS,
  REGISTER_FIELDS,
  charCount,
  mapServerErrors,
  toRegisterRequest,
  validateRegistration,
  type FieldErrors,
  type RegisterField,
  type RegisterValues,
} from './validation';
import styles from './AuthForm.module.css';

const EMPTY: RegisterValues = {
  handle: '',
  name: '',
  password: '',
  category: '',
  branch: '',
  location: '',
  about: '',
};

export function RegisterPage() {
  useDocumentTitle('Create account');
  const { signIn } = useAuth();
  const taxonomy = useTaxonomy();
  const [values, setValues] = useState<RegisterValues>(EMPTY);
  const [errors, setErrors] = useState<FieldErrors<RegisterField>>({});
  const [formError, setFormError] = useState<string | null>(null);
  const [showPassword, setShowPassword] = useState(false);
  const countdown = useCountdown();
  const formRef = useRef<HTMLFormElement>(null);

  // Every control is named after its field, so the first invalid one can be found by name.
  const focusFirstError = (fieldErrors: FieldErrors<RegisterField>) => {
    const first = REGISTER_FIELDS.find((field) => fieldErrors[field]);
    const control = first ? formRef.current?.elements.namedItem(first) : null;
    if (control instanceof HTMLElement) control.focus();
  };

  const update = <K extends keyof RegisterValues>(field: K, value: RegisterValues[K]) => {
    setValues((current) => ({ ...current, [field]: value }));
  };

  const onCategoryChange = (category: string) => {
    setValues((current) => {
      const keepBranch =
        current.branch !== '' && taxonomy.data?.branch_categories[current.branch] === category;
      return { ...current, category, branch: keepBranch ? current.branch : '' };
    });
  };

  const onBranchChange = (branch: string) => {
    setValues((current) => ({
      ...current,
      branch,
      // Choosing a technology also selects the category it belongs to.
      category:
        (branch === '' ? undefined : taxonomy.data?.branch_categories[branch]) ?? current.category,
    }));
  };

  const handleError = (error: Error) => {
    if (!isApiError(error)) {
      setFormError(describeError(error).message);
      return;
    }
    if (error.code === 'conflict' || error.status === 409) {
      const next = { handle: 'That handle is already taken. Try another one.' };
      setErrors(next);
      focusFirstError(next);
      return;
    }
    if (error.code === 'validation_failed' && error.details.length > 0) {
      const mapped = mapServerErrors(error.details, REGISTER_FIELDS);
      setErrors(mapped.fieldErrors);
      setFormError(
        mapped.other.length > 0
          ? `Please correct the following: ${mapped.other.join('; ')}`
          : 'Please correct the highlighted fields.',
      );
      focusFirstError(mapped.fieldErrors);
      return;
    }
    if (error.code === 'rate_limited') {
      setFormError(describeError(error).message);
      if (error.retryAfterSeconds !== null) countdown.start(error.retryAfterSeconds);
      return;
    }
    const description = describeError(error);
    setFormError(`${description.title}. ${description.message}`);
  };

  const register = useMutation({
    mutationFn: authApi.register,
    onSuccess: (auth) => {
      // The graph profile is created asynchronously; <ProfileGate> waits for it.
      signIn(auth);
    },
    onError: handleError,
  });

  const onSubmit = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    setFormError(null);
    const nextErrors = validateRegistration(values, taxonomy.data);
    setErrors(nextErrors);
    if (Object.keys(nextErrors).length > 0) {
      focusFirstError(nextErrors);
      return;
    }
    register.mutate(toRegisterRequest(values));
  };

  const aboutLength = charCount(values.about.trim());
  const locked = countdown.remaining > 0;
  const taxonomyReady = taxonomy.data !== undefined;

  return (
    <AuthLayout
      title="Create your account"
      subtitle="Join the network to see where you fit and who to connect with."
      footer={
        <>
          Already have an account? <Link to="/sign-in">Sign in</Link>
        </>
      }
    >
      <form ref={formRef} className={styles.form} onSubmit={onSubmit} noValidate>
        {formError ? <FormAlert tone="error">{formError}</FormAlert> : null}
        <TextField
          label="Handle"
          name="handle"
          autoComplete="username"
          autoCapitalize="none"
          autoCorrect="off"
          spellCheck={false}
          hint="3–32 lowercase letters, numbers or underscores. Used to sign in."
          value={values.handle}
          onChange={(event) => {
            update('handle', event.target.value.toLowerCase());
          }}
          error={errors.handle}
          required
        />
        <TextField
          label="Display name"
          name="name"
          autoComplete="name"
          value={values.name}
          onChange={(event) => {
            update('name', event.target.value);
          }}
          error={errors.name}
          required
        />
        <TextField
          label="Password"
          name="password"
          type={showPassword ? 'text' : 'password'}
          autoComplete="new-password"
          hint="At least 8 characters."
          value={values.password}
          onChange={(event) => {
            update('password', event.target.value);
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

        {taxonomy.isError ? (
          <div className={styles.taxonomyError}>
            <ErrorState
              compact
              error={taxonomy.error}
              title="Couldn't load categories"
              onRetry={() => void taxonomy.refetch()}
              retrying={taxonomy.isFetching}
            />
          </div>
        ) : (
          <div className={styles.row}>
            <SelectField
              label="Category"
              name="category"
              value={values.category}
              onChange={(event) => {
                onCategoryChange(event.target.value);
              }}
              error={errors.category}
              disabled={!taxonomyReady}
              aria-busy={taxonomy.isPending || undefined}
              required
            >
              <option value="">{taxonomyReady ? 'Choose a category' : 'Loading…'}</option>
              {taxonomy.data?.categories.map((category) => (
                <option key={category} value={category}>
                  {category}
                </option>
              ))}
            </SelectField>
            <SelectField
              label="Primary technology"
              name="branch"
              hint={values.category ? `Technologies in ${values.category}` : 'Grouped by category'}
              value={values.branch}
              onChange={(event) => {
                onBranchChange(event.target.value);
              }}
              error={errors.branch}
              disabled={!taxonomyReady}
              aria-busy={taxonomy.isPending || undefined}
              required
            >
              <option value="">{taxonomyReady ? 'Choose a technology' : 'Loading…'}</option>
              {taxonomy.data ? (
                <BranchOptions taxonomy={taxonomy.data} category={values.category} />
              ) : null}
            </SelectField>
          </div>
        )}

        <TextField
          label="Location"
          name="location"
          optional
          autoComplete="address-level2"
          value={values.location}
          onChange={(event) => {
            update('location', event.target.value);
          }}
          error={errors.location}
        />
        <TextAreaField
          label="About you"
          name="about"
          optional
          rows={3}
          hint={`Up to ${ABOUT_MAX_CHARS} characters.`}
          aside={
            <span
              className={[
                styles.counter,
                aboutLength > ABOUT_MAX_CHARS ? styles.counterOver : '',
              ].join(' ')}
              aria-hidden="true"
            >
              {aboutLength}/{ABOUT_MAX_CHARS}
            </span>
          }
          value={values.about}
          onChange={(event) => {
            update('about', event.target.value);
          }}
          error={errors.about}
        />
        <Button
          type="submit"
          variant="primary"
          className={styles.submit}
          loading={register.isPending}
          loadingLabel="Creating account…"
          disabled={locked}
        >
          {locked ? `Try again in ${countdown.remaining}s` : 'Create account'}
        </Button>
      </form>
    </AuthLayout>
  );
}
