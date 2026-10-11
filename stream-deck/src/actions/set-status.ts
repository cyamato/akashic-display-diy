import { action, SingletonAction, type DidReceiveSettingsEvent, type KeyDownEvent, type WillAppearEvent, type WillDisappearEvent } from '@elgato/streamdeck';
import type { RoomWatch } from '../watch.js';
import { draw, KeyRegistry, type KeySettings } from './draw.js';

/**
 * Set Status: one key per state (Busy, On the Phone, In a Meeting, Available or Auto).
 * Pressing it sets the room; it's lit while the room is in its state, whoever set it.
 */
@action({ UUID: 'io.github.cyamato.akashicbusy.set' })
export class SetStatus extends SingletonAction<KeySettings> {
  private readonly keys = new KeyRegistry();

  constructor(private readonly watch: RoomWatch) {
    super();
    watch.onChange((view) => {
      for (const a of this.actions) if (a.isKey()) void draw(a, this.keys.get(a.id).state, view);
    });
  }

  override async onWillAppear(ev: WillAppearEvent<KeySettings>): Promise<void> {
    const { state } = this.keys.put(ev.action.id, ev.payload.settings);
    if (ev.action.isKey()) await draw(ev.action, state, this.watch.current);
  }

  override onWillDisappear(ev: WillDisappearEvent<KeySettings>): void {
    this.keys.drop(ev.action.id);
  }

  override async onDidReceiveSettings(ev: DidReceiveSettingsEvent<KeySettings>): Promise<void> {
    const { state } = this.keys.put(ev.action.id, ev.payload.settings);
    if (ev.action.isKey()) await draw(ev.action, state, this.watch.current);
  }

  override async onKeyDown(ev: KeyDownEvent<KeySettings>): Promise<void> {
    const { state, minutes } = this.keys.put(ev.action.id, ev.payload.settings);
    try {
      await this.watch.set(state, minutes);
    } catch {
      await ev.action.showAlert();
    }
  }
}
