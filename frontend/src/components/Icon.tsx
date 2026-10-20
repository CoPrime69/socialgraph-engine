import type { SVGProps } from 'react';
import {
  ArrowClockwiseIcon,
  ArrowLeftIcon,
  ArrowRightIcon,
  ArrowSquareOutIcon,
  CaretDownIcon,
  CaretLeftIcon,
  CaretRightIcon,
  ChatCircleTextIcon,
  CheckIcon,
  ClockIcon,
  CornersOutIcon,
  EyeIcon,
  EyeSlashIcon,
  InfoIcon,
  LinkIcon,
  MagnifyingGlassIcon,
  MagnifyingGlassMinusIcon,
  MagnifyingGlassPlusIcon,
  MapPinIcon,
  MedalIcon,
  PlusIcon,
  ShareNetworkIcon,
  SignOutIcon,
  SquaresFourIcon,
  TableIcon,
  UserIcon,
  UserMinusIcon,
  UserPlusIcon,
  UsersThreeIcon,
  WarningCircleIcon,
  XIcon,
  type Icon as PhosphorIcon,
} from '@phosphor-icons/react';

/** The app's icon set, drawn from Phosphor (regular weight). Decorative by default. */
const ICONS = {
  overview: SquaresFourIcon,
  network: ShareNetworkIcon,
  communities: UsersThreeIcon,
  people: UserIcon,
  topics: ChatCircleTextIcon,
  search: MagnifyingGlassIcon,
  signOut: SignOutIcon,
  plus: PlusIcon,
  close: XIcon,
  check: CheckIcon,
  zoomIn: MagnifyingGlassPlusIcon,
  zoomOut: MagnifyingGlassMinusIcon,
  fit: CornersOutIcon,
  table: TableIcon,
  arrowLeft: ArrowLeftIcon,
  arrowRight: ArrowRightIcon,
  chevronLeft: CaretLeftIcon,
  chevronRight: CaretRightIcon,
  chevronDown: CaretDownIcon,
  alert: WarningCircleIcon,
  info: InfoIcon,
  refresh: ArrowClockwiseIcon,
  eye: EyeIcon,
  eyeOff: EyeSlashIcon,
  userPlus: UserPlusIcon,
  userMinus: UserMinusIcon,
  external: ArrowSquareOutIcon,
  mapPin: MapPinIcon,
  link: LinkIcon,
  award: MedalIcon,
  clock: ClockIcon,
} satisfies Record<string, PhosphorIcon>;

export type IconName = keyof typeof ICONS;

interface IconProps extends Omit<SVGProps<SVGSVGElement>, 'children' | 'ref'> {
  name: IconName;
  size?: number;
  /** Accessible label. Omit for decorative icons (the default), which are hidden from AT. */
  title?: string;
}

export function Icon({ name, size = 18, title, ...rest }: IconProps) {
  const Glyph = ICONS[name];
  const labelled = title !== undefined;
  return (
    <Glyph
      size={size}
      weight="regular"
      focusable="false"
      aria-hidden={labelled ? undefined : true}
      role={labelled ? 'img' : undefined}
      aria-label={labelled ? title : undefined}
      {...rest}
    />
  );
}
