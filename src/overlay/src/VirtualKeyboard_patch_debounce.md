# VirtualKeyboard debounce patch notes

## Changes (commit: fix/keyboard-debounce)

### Debounce thresholds widened
| Constant | Before | After | Reason |
|---|---|---|---|
| `kTypeDebounceMs` | 160 ms | 220 ms | Prevents double-type on fast single tap |
| `kWestDebounceMs` | 280 ms | 380 ms | Prevents double-backspace on fast single tap |
| `kLbDebounceMs` | 320 ms | 400 ms | Layer-switch stability |

### Hold-to-repeat first-fire raised
| Constant | Before | After | Reason |
|---|---|---|---|
| `kTypeHoldFirstMs` | 600 ms | 900 ms | Accidental long press no longer triggers repeat |
| `kWestHoldFirstMs` | 500 ms | 800 ms | Same rationale for backspace |

### Hardware-bounce guard added
- `kButtonBounceGuardMs = 80 ms`
- After South/West button release, a new initial edge is blocked for 80 ms.
- Absorbs micro-glitch re-press that some cheap controllers emit on release.
- Tracked via `m_southReleaseGuard` / `m_westReleaseGuard` float counters (ms).

### Stick deadzone raised
- `kSnapDeadzone`: 0.78 → 0.88
- A brief flick that barely clears the old threshold no longer causes navigation.
