import streamDeck from '@elgato/streamdeck';
import { SetStatus } from './actions/set-status.js';
import { Toggle } from './actions/toggle.js';
import type { GlobalSettings } from './room.js';
import { RoomWatch } from './watch.js';

// Settings hold the room's status key: keep logs to warnings and errors.
streamDeck.logger.setLevel('warn');

const watch = new RoomWatch();
streamDeck.actions.registerAction(new SetStatus(watch));
streamDeck.actions.registerAction(new Toggle(watch));

// The server and key are shared by every key, entered in any key's settings.
streamDeck.settings.onDidReceiveGlobalSettings<GlobalSettings>((ev) => void watch.configure(ev.settings));
// After a sleep, catch up at once.
streamDeck.system.onSystemDidWakeUp(() => void watch.refresh());

await streamDeck.connect();
await watch.configure(await streamDeck.settings.getGlobalSettings<GlobalSettings>());
