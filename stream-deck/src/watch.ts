/**
 * @fileoverview One watcher for the room, shared by every key: it asks the server every
 * few seconds (so a change from the calendar, a phone or another key shows), and tells the
 * keys when anything changes. Timers are injectable so it's tested without waiting.
 */
import { connectionFrom, getStatus, putStatus, StatusError, type Connection, type GlobalSettings, type RoomState, type RoomStatus } from './room.js';

/** What the keys draw from. */
export interface View {
  /** The connection is set up (else the keys say Set Up). */
  ready: boolean;
  status: RoomStatus | null;
  /** Why the last check failed (null: it didn't). */
  error: string | null;
}

/** How often the room is checked. */
export const POLL_MS = 10_000;

export interface Timers {
  setInterval: (fn: () => void, ms: number) => unknown;
  clearInterval: (handle: unknown) => void;
}
const realTimers: Timers = {
  setInterval: (fn, ms) => setInterval(fn, ms),
  clearInterval: (h) => clearInterval(h as ReturnType<typeof setInterval>),
};

export class RoomWatch {
  private connection: Connection | null = null;
  private handle: unknown = null;
  private view: View = { ready: false, status: null, error: null };
  private readonly listeners = new Set<(v: View) => void>();

  constructor(
    private readonly fetchImpl: typeof fetch = fetch,
    private readonly timers: Timers = realTimers,
    private readonly pollMs = POLL_MS,
  ) {}

  /** The latest view. */
  get current(): View {
    return this.view;
  }

  /** Hear each change. @return A function that stops listening */
  onChange(fn: (v: View) => void): () => void {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  }

  private publish(next: View) {
    this.view = next;
    for (const fn of this.listeners) fn(next);
  }

  /** Use these settings (the server and key): starts, restarts or stops watching. */
  async configure(settings: GlobalSettings | null | undefined): Promise<void> {
    const next = connectionFrom(settings);
    if (next?.server === this.connection?.server && next?.key === this.connection?.key) return;
    this.connection = next;
    if (this.handle !== null) this.timers.clearInterval(this.handle);
    this.handle = null;
    if (!next) {
      this.publish({ ready: false, status: null, error: null });
      return;
    }
    this.publish({ ready: true, status: null, error: null });
    this.handle = this.timers.setInterval(() => void this.refresh(), this.pollMs);
    await this.refresh();
  }

  /** Ask the server now. */
  async refresh(): Promise<void> {
    const c = this.connection;
    if (!c) return;
    try {
      const status = await getStatus(c, this.fetchImpl);
      if (c === this.connection) this.publish({ ready: true, status, error: null });
    } catch (err) {
      if (c === this.connection) this.publish({ ...this.view, error: (err as StatusError).message });
    }
  }

  /**
   * Set the room, and show the answer straight away.
   * @throws StatusError When it isn't set up, or the server refused
   */
  async set(state: RoomState, minutes: number): Promise<RoomStatus> {
    const c = this.connection;
    if (!c) throw new StatusError('not_set_up', 'Add the server and the room\'s status key in the key\'s settings');
    const status = await putStatus(c, state, minutes, this.fetchImpl);
    if (c === this.connection) this.publish({ ready: true, status, error: null });
    return status;
  }

  /** Stop checking. */
  stop(): void {
    if (this.handle !== null) this.timers.clearInterval(this.handle);
    this.handle = null;
  }
}
