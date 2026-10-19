import { describe, expect, it } from 'vitest';
import { taxonomy } from '../../test/fixtures';
import {
  byteLength,
  charCount,
  mapServerErrors,
  normalizeHandle,
  REGISTER_FIELDS,
  toRegisterRequest,
  validateAbout,
  validateHandle,
  validateLocation,
  validateName,
  validatePassword,
  validateRegistration,
  validateSignIn,
  type RegisterValues,
} from './validation';

const valid: RegisterValues = {
  handle: 'ada_member',
  name: 'Ada Member',
  password: 'correct-horse-battery',
  category: 'Web Development',
  branch: 'javascript',
  location: '',
  about: '',
};

describe('field rules (contracts §3)', () => {
  it('validates handles after trimming and lower-casing', () => {
    expect(normalizeHandle('  Ada_Member ')).toBe('ada_member');
    expect(validateHandle('Ada_Member')).toBeUndefined();
    expect(validateHandle('')).toBe('Enter a handle.');
    expect(validateHandle('ab')).toMatch(/3–32 characters/);
    expect(validateHandle('ada-member')).toMatch(/lowercase letters, numbers or underscores/);
    expect(validateHandle('a'.repeat(33))).toBeDefined();
  });

  it('validates names by code points and rejects control characters', () => {
    expect(validateName('Christian C. Salvadó')).toBeUndefined();
    expect(validateName('   ')).toBe('Enter your name.');
    expect(validateName('😀'.repeat(64))).toBeUndefined();
    expect(validateName('x'.repeat(65))).toBe('Use at most 64 characters.');
    expect(validateName('Ada\tMember')).toMatch(/control characters/);
  });

  it('validates password length in bytes', () => {
    expect(validatePassword('')).toBe('Enter a password.');
    expect(validatePassword('short')).toBe('Use at least 8 characters.');
    expect(validatePassword('éééé')).toBeUndefined(); // 8 bytes in UTF-8
    expect(validatePassword('x'.repeat(129))).toBe('Use at most 128 bytes.');
    expect(byteLength('é')).toBe(2);
    expect(charCount('😀é')).toBe(2);
  });

  it('limits location and about', () => {
    expect(validateLocation('Lisbon')).toBeUndefined();
    expect(validateLocation('x'.repeat(101))).toBe('Use at most 100 characters.');
    expect(validateLocation('Lis\nbon')).toMatch(/control characters/);
    expect(validateAbout('Line one\nLine two\twith a tab')).toBeUndefined();
    expect(validateAbout('x'.repeat(501))).toBe('Use at most 500 characters.');
    expect(validateAbout('bell\u0007')).toBe('Remove control characters.');
  });
});

describe('validateSignIn', () => {
  it('requires both fields', () => {
    expect(validateSignIn({ handle: ' ', password: '' })).toEqual({
      handle: 'Enter your handle.',
      password: 'Enter your password.',
    });
    expect(validateSignIn({ handle: 'ada', password: 'x' })).toEqual({});
  });
});

describe('validateRegistration', () => {
  it('accepts a complete form', () => {
    expect(validateRegistration(valid, taxonomy)).toEqual({});
  });

  it('requires a category and a technology from the taxonomy', () => {
    expect(validateRegistration({ ...valid, category: '', branch: '' }, taxonomy)).toEqual({
      category: 'Choose a category.',
      branch: 'Choose your primary technology.',
    });
    expect(validateRegistration({ ...valid, category: 'Gardening' }, taxonomy).category).toBe(
      'Choose a category from the list.',
    );
    expect(validateRegistration({ ...valid, branch: 'cobol' }, taxonomy).branch).toBe(
      'Choose a technology from the list.',
    );
  });

  it('requires the technology to belong to the chosen category', () => {
    expect(validateRegistration({ ...valid, branch: 'java' }, taxonomy).branch).toBe(
      'Choose a technology from Web Development.',
    );
  });

  it('skips taxonomy checks while the taxonomy is unavailable', () => {
    expect(validateRegistration({ ...valid, branch: 'cobol' }, undefined)).toEqual({});
  });
});

describe('toRegisterRequest', () => {
  it('normalises the handle, trims text and omits empty optional fields', () => {
    expect(toRegisterRequest({ ...valid, handle: ' Ada_Member ', name: ' Ada ' })).toEqual({
      handle: 'ada_member',
      name: 'Ada',
      password: 'correct-horse-battery',
      category: 'Web Development',
      branch: 'javascript',
    });
    expect(toRegisterRequest({ ...valid, location: ' Lisbon ', about: ' Hi ' })).toMatchObject({
      location: 'Lisbon',
      about: 'Hi',
    });
  });
});

describe('mapServerErrors', () => {
  it('maps details onto known fields and keeps the rest', () => {
    const mapped = mapServerErrors(
      [
        { field: 'handle', message: 'must be 3-32 characters' },
        { field: 'handle', message: 'second message is ignored' },
        { field: 'branch', message: 'unknown branch.' },
        { field: 'influence', message: 'not accepted' },
        { field: '', message: 'Something else' },
        { field: 'name', message: '  ' },
      ],
      REGISTER_FIELDS,
    );
    expect(mapped.fieldErrors).toEqual({
      handle: 'Must be 3-32 characters.',
      branch: 'Unknown branch.',
      name: 'This value is not valid.',
    });
    expect(mapped.other).toEqual(['influence: not accepted', 'Something else']);
  });
});
