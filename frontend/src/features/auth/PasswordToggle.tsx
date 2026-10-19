import { IconButton } from '../../components/Button';

interface PasswordToggleProps {
  visible: boolean;
  onToggle: () => void;
}

export function PasswordToggle({ visible, onToggle }: PasswordToggleProps) {
  return (
    <IconButton
      icon={visible ? 'eyeOff' : 'eye'}
      label={visible ? 'Hide password' : 'Show password'}
      aria-pressed={visible}
      size="sm"
      onClick={onToggle}
    />
  );
}
