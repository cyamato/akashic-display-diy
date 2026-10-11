import { describe, expect, it, vi } from 'vitest';
import { RoomWatch, type View } from '../src/watch.js';

const KEY = `pds_${'b'.repeat(43)}`;
const SETTINGS = { server: 'https://dash.example.com', key: KEY };
const room = (override: string | null) => ({ id: 'room-1', name: 'Home office', override, override_until: override ? '2026-10-08T11:00:00Z' : null });

function fakeTimers() {
  let tick: (() => void) | null = null;
  return {
    timers: { setInterval: vi.fn((fn: () => void) => { tick = fn; return 7; }), clearInterval: vi.fn(() => { tick = null; }) },
    fire: () => tick?.(),
  };
}

describe('RoomWatch', () => {
  it('checks the room when set up, again every few seconds, and tells every listener', async () => {
    let answer = room(null);
    const f = vi.fn(async () => Response.json(answer));
    const t = fakeTimers();
    const w = new RoomWatch(f as unknown as typeof fetch, t.timers, 5000);
    const seen: View[] = [];
    const stop = w.onChange((v) => seen.push(v));
    expect(w.current).toEqual({ ready: false, status: null, error: null });
    await w.configure(SETTINGS);
    expect(t.timers.setInterval).toHaveBeenCalledWith(expect.any(Function), 5000);
    expect(w.current).toEqual({ ready: true, status: room(null), error: null });
    // The calendar (or another key) changes it: the next check shows it.
    answer = room('meeting');
    t.fire();
    await vi.waitFor(() => expect(w.current.status?.override).toBe('meeting'));
    // The same settings again change nothing.
    await w.configure({ ...SETTINGS });
    expect(t.timers.setInterval).toHaveBeenCalledTimes(1);
    stop();
    const before = seen.length;
    await w.refresh();
    expect(seen).toHaveLength(before);
    w.stop();
    expect(t.timers.clearInterval).toHaveBeenCalledWith(7);
    w.stop();
  });

  it('sets the room and shows the answer at once; says why it can\'t', async () => {
    const f = vi.fn(async (_url: string, init: RequestInit = {}) => (init.method === 'PUT'
      ? Response.json(room(JSON.parse(String(init.body)).state))
      : Response.json(room(null))));
    const w = new RoomWatch(f as unknown as typeof fetch, fakeTimers().timers);
    await expect(w.set('busy', 60)).rejects.toMatchObject({ code: 'not_set_up' });
    await w.configure(SETTINGS);
    expect(await w.set('phone', 30)).toMatchObject({ override: 'phone' });
    expect(w.current.status?.override).toBe('phone');
  });

  it('keeps the last state when a check fails, with why', async () => {
    let fail = false;
    const w = new RoomWatch(vi.fn(async () => (fail ? Promise.reject(new TypeError('down')) : Response.json(room('busy')))) as unknown as typeof fetch, fakeTimers().timers);
    await w.configure(SETTINGS);
    fail = true;
    await w.refresh();
    expect(w.current).toEqual({ ready: true, status: room('busy'), error: 'The server could not be reached' });
  });

  it('stops when the settings are taken away, and ignores answers for old settings', async () => {
    let release!: (r: Response) => void;
    const t = fakeTimers();
    const w = new RoomWatch(vi.fn(() => new Promise<Response>((r) => { release = r; })) as unknown as typeof fetch, t.timers);
    const pending = w.configure(SETTINGS);
    await w.configure({ server: '', key: '' });
    expect(t.timers.clearInterval).toHaveBeenCalled();
    expect(w.current.ready).toBe(false);
    release(Response.json(room('busy')));
    await pending;
    expect(w.current).toEqual({ ready: false, status: null, error: null });
    // Not set up: nothing to check.
    await w.refresh();
    expect(w.current.ready).toBe(false);
  });

  it('ignores a failure or a set that lands after the settings changed', async () => {
    let reject!: (e: Error) => void;
    let resolvePut!: (r: Response) => void;
    let mode: 'get' | 'fail' | 'put' = 'get';
    const f = vi.fn((_url: string, init: RequestInit = {}) => {
      if (mode === 'fail') return new Promise<Response>((_, r) => { reject = r; });
      if (init.method === 'PUT') return new Promise<Response>((r) => { resolvePut = r; });
      return Promise.resolve(Response.json(room(null)));
    });
    const w = new RoomWatch(f as unknown as typeof fetch, fakeTimers().timers);
    await w.configure(SETTINGS);
    mode = 'fail';
    const check = w.refresh();
    mode = 'put';
    const set = w.set('busy', 10);
    mode = 'get';
    await w.configure({ server: 'https://other.example.com', key: KEY });
    reject(new TypeError('down'));
    resolvePut(Response.json(room('busy')));
    await check;
    await set;
    expect(w.current).toEqual({ ready: true, status: room(null), error: null });
  });
});
