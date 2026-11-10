# Themes

The application supports three theme preferences:

| Preference | Behavior |
|---|---|
| **System** | Follows the operating system's light/dark preference via `prefers-color-scheme`. |
| **Light** | Light surfaces and high-contrast dark text. |
| **Dark** | The original dark interface. |

Use the **Theme** selector in the top bar after unlocking, or on the unlock
screen before opening a database.

## Persistence

- The selected theme is saved in the encrypted database under
  `app_meta.ui.theme`.
- A local `localStorage` copy is also used when the webview exposes it, so the
  unlock screen can keep the last choice.
- If the database has no saved theme, the current choice is written when the
  database opens.
- The preference is stored only as `system`, `light`, or `dark`; it contains no
  sensitive data.

## Implementation

All colors are CSS custom properties in `src/web/style.css`:

- `:root` defines the dark palette.
- `html[data-theme="light"]` overrides the same variables.
- `@media (prefers-color-scheme: light)` applies the light overrides when the
  preference is `system`.

The JavaScript theme helper sets `document.documentElement.dataset.theme` and
synchronizes both theme selectors. The APIs are:

```json
{ "method": "prefs.get", "params": {} }
{ "method": "prefs.set", "params": { "theme": "light" } }
```

`prefs.get` returns `theme` and `persisted`. If no preference has been saved,
`persisted` is false and the UI keeps the local/system choice.
