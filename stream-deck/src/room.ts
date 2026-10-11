/**
 * @fileoverview Talking to an Akashic Display server about one room, with that room's
 * status key: what it is now (busy, on the phone, in a meeting, available, or following
 * its calendar), and telling it. Nothing here depends on the Stream Deck SDK, so it is
 * tested on its own.
 */

/** What a key can set: busy for a reason, available, or back to the room's calendar. */
export type RoomState = 'busy' | 'phone' | 'meeting' | 'free' | 'auto';
export const STATES: readonly RoomState[] = ['busy', 'phone', 'meeting', 'free', 'auto'];

/** Each state's name, and the shorter one that fits on a key. */
export const LABELS: Record<RoomState, string> = { busy: 'Busy', phone: 'On the Phone', meeting: 'In a Meeting', free: 'Available', auto: 'Auto' };
export const SHORT: Record<RoomState, string> = { busy: 'Busy', phone: 'On Phone', meeting: 'Meeting', free: 'Free', auto: 'Auto' };

/** Where the room lives: the server's address and the room's status key (from /admin → Rooms). */
export interface Connection { server: string; key: string }

/** The plugin's settings shared by every key. */
export type GlobalSettings = { server?: string; key?: string };

/** What the server says about the room. */
export interface RoomStatus {
  id: string;
  name: string;
  override: Exclude<RoomState, 'auto'> | null;
  override_until: string | null;
}

/** How long a state lasts when a key doesn't say (the server's own default too). */
export const DEFAULT_MINUTES = 60;
export const MAX_MINUTES = 24 * 60;

const KEY_PATTERN = /^pds_[A-Za-z0-9_-]{20,}$/;
const LOCAL_HOSTS = new Set(['localhost', '127.0.0.1', '[::1]']);

/**
 * The connection in the plugin's settings, if it's complete and sensible: an https
 * address (http only on this computer) and a room status key.
 * @return The connection, or null
 */
export function connectionFrom(settings: GlobalSettings | null | undefined): Connection | null {
  const raw = (settings?.server ?? '').trim();
  const key = (settings?.key ?? '').trim();
  if (!raw || !KEY_PATTERN.test(key)) return null;
  let url: URL;
  try {
    url = new URL(raw);
  } catch {
    return null;
  }
  if (url.protocol !== 'https:' && !(url.protocol === 'http:' && LOCAL_HOSTS.has(url.hostname))) return null;
  return { server: url.origin, key };
}

/** A key's own settings, made valid: its state and for how many minutes. */
export function cleanKeySettings(raw: { state?: unknown; minutes?: unknown } | null | undefined): { state: RoomState; minutes: number } {
  const state = STATES.includes(raw?.state as RoomState) ? raw!.state as RoomState : 'busy';
  const n = Number(raw?.minutes);
  const minutes = Number.isFinite(n) && n >= 1 ? Math.min(MAX_MINUTES, Math.round(n)) : DEFAULT_MINUTES;
  return { state, minutes };
}

/** The server refused or couldn't be reached. */
export class StatusError extends Error {
  constructor(readonly code: string, message: string) {
    super(message);
    this.name = 'StatusError';
  }
}

async function call(c: Connection, init: RequestInit, fetchImpl: typeof fetch): Promise<RoomStatus> {
  let res: Response;
  try {
    res = await fetchImpl(`${c.server}/v1/rooms/status`, {
      ...init,
      headers: { Authorization: `Bearer ${c.key}`, ...(init.body ? { 'Content-Type': 'application/json' } : {}) },
      signal: AbortSignal.timeout(10_000),
    });
  } catch {
    throw new StatusError('offline', 'The server could not be reached');
  }
  const body = (await res.json().catch(() => null)) as (Partial<RoomStatus> & { error_code?: string; message?: string }) | null;
  if (!res.ok) throw new StatusError(body?.error_code ?? `http_${res.status}`, body?.message ?? `The server said ${res.status}`);
  if (!body || typeof body.id !== 'string') throw new StatusError('bad_answer', 'The server sent something unexpected');
  return { id: body.id, name: String(body.name ?? ''), override: body.override ?? null, override_until: body.override_until ?? null };
}

/** What the room is now. */
export const getStatus = (c: Connection, fetchImpl: typeof fetch = fetch) => call(c, { method: 'GET' }, fetchImpl);

/**
 * Set the room: a state for so many minutes, or Auto (its calendar again).
 * @return What the room is now
 */
export function putStatus(c: Connection, state: RoomState, minutes: number, fetchImpl: typeof fetch = fetch): Promise<RoomStatus> {
  return call(c, { method: 'PUT', body: JSON.stringify(state === 'auto' ? { state } : { state, minutes }) }, fetchImpl);
}

/** The state in force now: what someone said, until it ends, else Auto. */
export function liveState(status: RoomStatus | null, now: Date): RoomState {
  if (!status?.override || !status.override_until || Date.parse(status.override_until) <= now.getTime()) return 'auto';
  return status.override;
}

/** The order the Toggle key steps through. */
export const CYCLE: readonly RoomState[] = ['free', 'phone', 'meeting', 'busy', 'auto'];
/** The state after this one on the Toggle key. */
export const nextState = (current: RoomState): RoomState => CYCLE[(CYCLE.indexOf(current) + 1) % CYCLE.length]!;

/** "11:30" in this computer's clock. */
const clock = (iso: string) => new Date(iso).toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });

/**
 * A key's title: its state's short name, and until when while it's in force.
 * @param state The state the key stands for (the Toggle key: the room's own).
 */
export function keyTitle(state: RoomState, status: RoomStatus | null, now: Date): string {
  const lit = liveState(status, now) === state;
  return lit && state !== 'auto' && status?.override_until ? `${SHORT[state]}\n${clock(status.override_until)}` : SHORT[state];
}

/** Each state's colour on a lit key. */
export const COLORS: Record<RoomState, string> = { busy: '#dc2626', phone: '#dc2626', meeting: '#dc2626', free: '#16a34a', auto: '#2f5bd3' };

/** Each state's picture: a few strokes in a 72×72 box, drawn in white. */
const GLYPHS: Record<RoomState, string> = {
  busy: '<circle cx="36" cy="30" r="14" fill="none" stroke="#fff" stroke-width="5"/><path d="M26 30h20" stroke="#fff" stroke-width="5" stroke-linecap="round"/>',
  phone: '<path d="M25 18c3-2 6-1 7 2l2 6c1 3 0 5-2 6l-2 1c2 5 5 8 10 10l1-2c1-2 3-3 6-2l6 2c3 1 4 4 2 7l-2 3c-2 2-5 3-8 2-12-4-20-12-24-24-1-3 0-6 2-8z" fill="#fff"/>',
  meeting: '<circle cx="27" cy="22" r="7" fill="#fff"/><circle cx="45" cy="22" r="7" fill="#fff"/><path d="M15 44c0-8 5-13 12-13s12 5 12 13zM33 44c0-8 5-13 12-13s12 5 12 13z" fill="#fff"/>',
  free: '<path d="M22 31l9 9 18-18" fill="none" stroke="#fff" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>',
  auto: '<rect x="20" y="17" width="32" height="28" rx="4" fill="none" stroke="#fff" stroke-width="4"/><path d="M20 26h32M28 13v8M44 13v8" stroke="#fff" stroke-width="4" stroke-linecap="round"/>',
};

/**
 * A key's picture: its state's colour when lit, grey when not, as an SVG data URL.
 * The title (set separately) sits under the picture.
 */
export function keyImage(state: RoomState, lit: boolean): string {
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="72" height="72" viewBox="0 0 72 72"><rect width="72" height="72" rx="10" fill="${lit ? COLORS[state] : '#2b2f3a'}"/><g opacity="${lit ? 1 : 0.55}">${GLYPHS[state]}</g></svg>`;
  return `data:image/svg+xml;charset=utf8,${encodeURIComponent(svg)}`;
}
