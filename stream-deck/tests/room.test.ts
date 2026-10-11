import { describe, expect, it, vi } from 'vitest';
import {
  cleanKeySettings, COLORS, connectionFrom, CYCLE, getStatus, keyImage, keyTitle, LABELS, liveState, nextState, putStatus, SHORT, STATES, StatusError, type RoomStatus,
} from '../src/room.js';

const KEY = `pds_${'a'.repeat(43)}`;
const C = { server: 'https://dash.example.com', key: KEY };
const NOW = new Date('2026-10-08T10:00:00Z');
const status = (o: Partial<RoomStatus> = {}): RoomStatus => ({ id: 'room-1', name: 'Home office', override: null, override_until: null, ...o });

describe('connectionFrom', () => {
  it('takes an https server (http only on this computer) and a room status key', () => {
    expect(connectionFrom({ server: ' https://dash.example.com/some/page ', key: ` ${KEY} ` })).toEqual(C);
    expect(connectionFrom({ server: 'http://localhost:8443', key: KEY })).toEqual({ server: 'http://localhost:8443', key: KEY });
    expect(connectionFrom({ server: 'http://dash.example.com', key: KEY })).toBeNull();
    expect(connectionFrom({ server: 'not a url', key: KEY })).toBeNull();
    expect(connectionFrom({ server: 'https://dash.example.com', key: 'pdt_wrong' })).toBeNull();
    expect(connectionFrom({ key: KEY })).toBeNull();
    expect(connectionFrom(null)).toBeNull();
  });
});

describe('cleanKeySettings', () => {
  it('keeps a known state and sensible minutes, else Busy for an hour', () => {
    expect(cleanKeySettings({ state: 'phone', minutes: '30' })).toEqual({ state: 'phone', minutes: 30 });
    expect(cleanKeySettings({ state: 'nap', minutes: 0 })).toEqual({ state: 'busy', minutes: 60 });
    expect(cleanKeySettings({ minutes: 99_999 })).toEqual({ state: 'busy', minutes: 1440 });
    expect(cleanKeySettings(undefined)).toEqual({ state: 'busy', minutes: 60 });
  });
});

describe('the server', () => {
  it('reads and sets the room with its status key', async () => {
    const f = vi.fn(async (_url: string, _init?: RequestInit) => Response.json(status({ override: 'meeting', override_until: '2026-10-08T11:00:00Z' })));
    expect(await getStatus(C, f as unknown as typeof fetch)).toMatchObject({ override: 'meeting' });
    expect(f).toHaveBeenLastCalledWith('https://dash.example.com/v1/rooms/status', expect.objectContaining({ method: 'GET', headers: { Authorization: `Bearer ${KEY}` } }));
    await putStatus(C, 'phone', 30, f as unknown as typeof fetch);
    expect(f.mock.calls.at(-1)![1]).toMatchObject({ method: 'PUT', body: '{"state":"phone","minutes":30}', headers: { 'Content-Type': 'application/json' } });
    await putStatus(C, 'auto', 30, f as unknown as typeof fetch);
    expect(f.mock.calls.at(-1)![1]).toMatchObject({ body: '{"state":"auto"}' });
    // A missing name or override is filled in.
    expect(await getStatus(C, vi.fn(async () => Response.json({ id: 'room-1' })) as unknown as typeof fetch)).toEqual(status({ name: '' }));
  });

  it('explains refusals, odd answers and an unreachable server', async () => {
    const fail = (f: () => Promise<Response>) => getStatus(C, vi.fn(f) as unknown as typeof fetch).catch((e: StatusError) => [e.code, e.message]);
    expect(await fail(async () => Response.json({ error_code: 'unauthorized', message: 'A valid status key is required' }, { status: 401 }))).toEqual(['unauthorized', 'A valid status key is required']);
    expect(await fail(async () => new Response('nope', { status: 502 }))).toEqual(['http_502', 'The server said 502']);
    expect(await fail(async () => Response.json({ hello: 1 }))).toEqual(['bad_answer', 'The server sent something unexpected']);
    expect(await fail(async () => new Response('not json'))).toEqual(['bad_answer', 'The server sent something unexpected']);
    expect(await fail(async () => { throw new TypeError('fetch failed'); })).toEqual(['offline', 'The server could not be reached']);
  });
});

describe('what a key shows', () => {
  it('knows the state in force: what someone said until it ends, else Auto', () => {
    expect(liveState(status({ override: 'phone', override_until: '2026-10-08T10:30:00Z' }), NOW)).toBe('phone');
    expect(liveState(status({ override: 'phone', override_until: '2026-10-08T09:30:00Z' }), NOW)).toBe('auto');
    expect(liveState(status({ override: 'busy' }), NOW)).toBe('auto');
    expect(liveState(status(), NOW)).toBe('auto');
    expect(liveState(null, NOW)).toBe('auto');
  });

  it('steps the Toggle key round every state', () => {
    expect(CYCLE.map(nextState)).toEqual(['phone', 'meeting', 'busy', 'auto', 'free']);
    expect([...CYCLE].sort()).toEqual([...STATES].sort());
  });

  it('titles a key with its state, and until when while it\'s in force', () => {
    const meeting = status({ override: 'meeting', override_until: '2026-10-08T11:00:00Z' });
    expect(keyTitle('meeting', meeting, NOW)).toMatch(/^Meeting\n\d{1,2}:00/);
    expect(keyTitle('phone', meeting, NOW)).toBe('On Phone');
    expect(keyTitle('auto', status(), NOW)).toBe('Auto');
    expect(Object.keys(SHORT)).toEqual(Object.keys(LABELS));
  });

  it('draws each state in its colour when lit, grey when not', () => {
    const lit = decodeURIComponent(keyImage('free', true));
    expect(lit).toMatch(/^data:image\/svg\+xml;charset=utf8,<svg/);
    expect(lit).toContain(`fill="${COLORS.free}"`);
    expect(decodeURIComponent(keyImage('free', false))).toContain('fill="#2b2f3a"');
    for (const s of STATES) expect(decodeURIComponent(keyImage(s, true))).toContain('<g opacity="1">');
  });
});
