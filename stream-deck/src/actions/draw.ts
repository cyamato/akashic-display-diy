/**
 * @fileoverview Drawing a key from the room's view, and keeping each visible key's own
 * settings at hand so every key can be redrawn when the room changes.
 */
import type { KeyAction } from '@elgato/streamdeck';
import { cleanKeySettings, keyImage, keyTitle, liveState, type RoomState } from '../room.js';
import type { View } from '../watch.js';

/** A key's own settings, as the Stream Deck stores them. */
export type KeySettings = { state?: RoomState; minutes?: number };

/**
 * Draw a key: the state it stands for, lit when the room is in it.
 * @param state The key's state (the Toggle key: the room's own).
 */
export async function draw(key: KeyAction<KeySettings>, state: RoomState, view: View, now = new Date()): Promise<void> {
  if (!view.ready) {
    await key.setImage(keyImage(state, false));
    await key.setTitle('Set Up');
    return;
  }
  await key.setImage(keyImage(state, liveState(view.status, now) === state));
  await key.setTitle(keyTitle(state, view.status, now));
}

/** Each visible key's settings, by its id. */
export class KeyRegistry {
  private readonly settings = new Map<string, { state: RoomState; minutes: number }>();

  put(id: string, raw: KeySettings | null | undefined) {
    const clean = cleanKeySettings(raw);
    this.settings.set(id, clean);
    return clean;
  }

  get(id: string) {
    return this.settings.get(id) ?? cleanKeySettings(null);
  }

  drop(id: string) {
    this.settings.delete(id);
  }
}
