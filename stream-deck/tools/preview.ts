// Write an HTML page of every key, lit and not, for checking the pictures (node --experimental-transform-types tools/preview.ts > preview.html).
import { keyImage, keyTitle, SHORT, STATES } from '../src/room.ts';

const cell = (s: (typeof STATES)[number], lit: boolean) => `<figure><img src="${keyImage(s, lit)}" width="144" height="144"><figcaption>${lit ? keyTitle(s, { id: 'r', name: 'r', override: s === 'auto' ? null : s, override_until: '2026-10-08T23:00:00Z' }, new Date('2026-10-08T10:00:00Z')).replace('\n', '<br>') : SHORT[s]}</figcaption></figure>`;
console.log(`<!doctype html><body style="background:#111;color:#fff;font:14px sans-serif;display:grid;grid-template-columns:repeat(5,160px);gap:12px;padding:16px">${STATES.map((s) => cell(s, true)).join('')}${STATES.map((s) => cell(s, false)).join('')}<style>figure{margin:0;text-align:center}img{border-radius:12px}</style></body>`);
