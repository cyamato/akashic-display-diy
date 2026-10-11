import { action, SingletonAction, type DidReceiveSettingsEvent, type KeyDownEvent, type WillAppearEvent, type WillDisappearEvent } from '@elgato/streamdeck';
import { liveState, nextState } from '../room.js';
import type { RoomWatch } from '../watch.js';
import { draw, KeyRegistry, type KeySettings } from './draw.js';

/**
 * Toggle: one key that shows the room's state and steps to the next on each press
 * (Available, On the Phone, In a Meeting, Busy, Auto).
 */
@action({ UUID: 'io.github.cyamato.akashicbusy.toggle' })
export class Toggle extends SingletonAction<KeySettings> {
  private readonly keys = new KeyRegistry();

  constructor(private readonly watch: RoomWatch) {
    super();
    watch.onChange((view) => {
      for (const a of this.actions) if (a.isKey()) void draw(a, liveState(view.status, new Date()), view);
    });
  }

  private now() {
    return liveState(this.watch.current.status, new Date());
  }

  override async onWillAppear(ev: WillAppearEvent<KeySettings>): Promise<void> {
    this.keys.put(ev.action.id, ev.payload.settings);
    if (ev.action.isKey()) await draw(ev.action, this.now(), this.watch.current);
  }

  override onWillDisappear(ev: WillDisappearEvent<KeySettings>): void {
    this.keys.drop(ev.action.id);
  }

  override onDidReceiveSettings(ev: DidReceiveSettingsEvent<KeySettings>): void {
    this.keys.put(ev.action.id, ev.payload.settings);
  }

  override async onKeyDown(ev: KeyDownEvent<KeySettings>): Promise<void> {
    const { minutes } = this.keys.put(ev.action.id, ev.payload.settings);
    try {
      await this.watch.set(nextState(this.now()), minutes);
    } catch {
      await ev.action.showAlert();
    }
  }
}
