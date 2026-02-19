# Web Redesign Mapping (Prototype -> Vue)

## Source of Truth

- Web prototype: `realliveapp/prototypes/server-web/index.html`
- Web design notes: `realliveapp/prototypes/server-web/UI_DESIGN.md`

## Layout Mapping

| Prototype Block | Vue Target |
| --- | --- |
| `app-shell` | `server/web/src/components/layout/AppShell.vue` |
| `sidebar` + `topbar` | `server/web/src/components/layout/AppShell.vue` |
| `content` | `server/web/src/components/layout/AppShell.vue` slot |

## Route Mapping

| Prototype Page | Vue Route |
| --- | --- |
| Dashboard | `/dashboard` |
| Live Monitor | `/monitor` |
| History Playback | `/playback` |
| Alert Center | `/alerts` |
| Device Management | `/devices` |
| Storage Management | `/storage` |
| System Settings | `/settings` |
| Login | `/login` |

## Token Mapping

- Token source: `server/web/src/styles/tokens.css`
- Global base styles: `server/web/src/styles/global.css`
- Mandatory values align with prototype palette and spacing system.

## Conflict Rule

- If current UI conflicts with prototype layout or interaction, current UI is removed.
- Existing runtime capabilities are re-mounted into prototype layout (not the reverse).

## Current Phase

- Phase A/B start completed:
  - App shell rebuilt to prototype structure.
  - Route structure normalized to `/dashboard` root model.
  - Login page kept and aligned to prototype style baseline.
- Phase 5 completed:
  - Dashboard rebuilt with prototype-aligned sections:
    - Stats row
    - Quick access grid
    - Site map overview
    - Camera grid + alert panel
    - Trend chart card
  - Data is now passed through a design adapter:
    - `server/web/src/api/design-adapters.js`
- Phase 6 completed:
  - Monitor page rebuilt to prototype layout and integrated with existing runtime:
    - camera selection
    - live flv playback
    - watch session start/heartbeat/stop
    - status strip and control side panel
- Phase 7 (Playback) completed:
  - Playback page rebuilt to prototype structure and connected to existing history APIs:
    - date calendar + single camera selection
    - recorded history timeline for selected day
    - filmstrip thumbnails embedded in timeline track
    - event markers + event list jump seek
    - history replay playback via existing `history/play` endpoint
- Phase 8 (Alerts) completed:
  - Alert Center rebuilt to prototype structure and connected to existing alert APIs:
    - stats cards + type chips + time-range filter
    - alert table with selection and pagination
    - batch operations (`read`, `resolve`, `delete`)
    - row quick actions and alert detail drawer
- Phase 9 (Devices) completed:
  - Device Management rebuilt to prototype structure and connected to existing camera APIs/store:
    - status stats row + status filter chips
    - table/card dual view
    - selection and batch delete
    - add/edit modal and single delete
    - quick jump to live watch and playback pages
- Phase 10 (Storage) completed:
  - Storage page rebuilt to prototype structure and connected to existing storage APIs:
    - overview ring + breakdown cards
    - trend bars from backend trend data
    - per-device storage table
    - storage policy controls + save action
- Phase 11 (Settings) completed:
  - Settings page rebuilt to prototype tabbed structure with interactive controls:
    - profile edit/reset/save
    - security toggles + active session list from backend session API
    - users/roles and audit sections aligned to design
    - notification channel toggles and policy form
    - system runtime panel with backend health + camera stats
- Phase 12 (Integration polish) completed:
  - Cross-page interaction and persistence fixes:
    - Devices -> Playback jump now carries `cameraId`
    - Playback reads `cameraId` query for initial camera selection
    - Devices add/edit/delete flow polished (status apply on create, modal close on delete)
    - Settings form data persists to local storage and restores on reload
    - Alerts selection state auto-pruned after data refresh
- Phase 13 (Runtime polish) completed:
  - Alert Center and Storage behavior refinements:
    - Alerts auto-refresh every 10s with last-updated indicator
    - Storage policy switches aligned with backend capability (removed misleading disable behavior for numeric retention fields)
- Phase 14 (Playback UX fix) completed:
  - Playback first-load date alignment:
    - if selected day has no overlap but camera has history, auto-jump to latest history date
    - camera switch resets this alignment logic per camera
