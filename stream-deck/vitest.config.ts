import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    include: ['tests/**/*.test.ts'],
    coverage: {
      // The Stream Deck glue (actions and plugin.ts) needs the Stream Deck app; the logic it uses is covered here.
      include: ['src/room.ts', 'src/watch.ts'],
      reporter: ['text', 'lcov'],
      reportsDirectory: 'coverage',
      thresholds: { lines: 100, branches: 95 },
    },
  },
});
