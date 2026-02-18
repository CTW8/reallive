<script setup>
import { ref } from 'vue'

const viewMode = ref('table')

const devices = [
  { id: 1, name: 'Front Door Camera', location: 'Main Entrance', ip: '192.168.1.101', status: 'online', model: 'RL-CAM-1080' },
  { id: 2, name: 'Parking Lot B2', location: 'Underground Level 2', ip: '192.168.1.102', status: 'recording', model: 'RL-CAM-1080' },
  { id: 3, name: 'Lobby - Reception', location: 'Building A, 1F', ip: '192.168.1.103', status: 'online', model: 'RL-CAM-4K' },
  { id: 4, name: 'Warehouse East', location: 'East Wing', ip: '192.168.1.104', status: 'offline', model: 'RL-CAM-1080' },
  { id: 5, name: 'Corridor 3F - North', location: 'Building A, 3F', ip: '192.168.1.105', status: 'online', model: 'RL-CAM-720' },
]
</script>

<template>
  <div class="devices-page">
    <div class="device-toolbar">
      <div class="dt-left">
        <div class="filter-chips">
          <button class="filter-chip active">All <span class="chip-count">12</span></button>
          <button class="filter-chip">Online <span class="chip-count">9</span></button>
          <button class="filter-chip">Recording <span class="chip-count">4</span></button>
          <button class="filter-chip">Offline <span class="chip-count">3</span></button>
        </div>
      </div>
      <div class="dt-right">
        <div class="view-toggle">
          <button class="vt-btn" :class="{ active: viewMode === 'table' }" @click="viewMode = 'table'">
            <span class="mi">table_rows</span>
          </button>
          <button class="vt-btn" :class="{ active: viewMode === 'card' }" @click="viewMode = 'card'">
            <span class="mi">grid_view</span>
          </button>
        </div>
        <button class="mt-btn primary"><span class="mi">add</span> Add Device</button>
      </div>
    </div>

    <div class="device-stats-row">
      <div class="alert-stat-mini">
        <div class="asm-icon" style="background:rgba(125,216,129,.12);color:var(--green)">
          <span class="mi">videocam</span>
        </div>
        <div class="asm-info">
          <h4>9</h4>
          <p>Online</p>
        </div>
      </div>
      <div class="alert-stat-mini">
        <div class="asm-icon" style="background:rgba(255,107,107,.12);color:var(--red)">
          <span class="mi">videocam_off</span>
        </div>
        <div class="asm-info">
          <h4>3</h4>
          <p>Offline</p>
        </div>
      </div>
      <div class="alert-stat-mini">
        <div class="asm-icon" style="background:rgba(255,158,67,.12);color:var(--orange)">
          <span class="mi">fiber_manual_record</span>
        </div>
        <div class="asm-info">
          <h4>4</h4>
          <p>Recording</p>
        </div>
      </div>
      <div class="alert-stat-mini">
        <div class="asm-icon" style="background:rgba(200,191,255,.12);color:var(--pri)">
          <span class="mi">update</span>
        </div>
        <div class="asm-info">
          <h4>3</h4>
          <p>Updates Available</p>
        </div>
      </div>
    </div>

    <div v-if="viewMode === 'table'" class="device-table-wrap">
      <div class="device-table">
        <div class="dt-head">
          <div class="dt-cb"><input type="checkbox"></div>
          <div class="sortable">Device <span class="mi">unfold_more</span></div>
          <div>Location</div>
          <div>IP Address</div>
          <div>Status</div>
          <div>Model</div>
          <div></div>
        </div>
        <div v-for="device in devices" :key="device.id" class="dt-row">
          <div class="dt-cb"><input type="checkbox"></div>
          <div class="dt-device">
            <div class="dev-thumb"><span class="mi">videocam</span></div>
            <div class="dev-info">
              <h4>{{ device.name }}</h4>
              <p>{{ device.ip }}</p>
            </div>
          </div>
          <div class="dt-cell">{{ device.location }}</div>
          <div class="dt-cell">{{ device.ip }}</div>
          <div class="dt-status">
            <span class="dot" :class="{ on: device.status === 'online', off: device.status === 'offline', rec: device.status === 'recording' }"></span>
            {{ device.status }}
          </div>
          <div class="dt-cell">{{ device.model }}</div>
          <div class="dt-actions">
            <button class="act-btn"><span class="mi">settings</span></button>
            <button class="act-btn"><span class="mi">videocam</span></button>
            <button class="act-btn"><span class="mi">more_vert</span></button>
          </div>
        </div>
      </div>
    </div>

    <div v-else class="device-card-grid">
      <div v-for="device in devices" :key="device.id" class="dev-card">
        <div class="dc-thumb">
          <div class="sv-grid"></div>
          <span class="mi">videocam</span>
          <div class="dc-status-badge" :class="device.status">
            <span class="dot"></span>
            {{ device.status.toUpperCase() }}
          </div>
        </div>
        <div class="dc-body">
          <h4>{{ device.name }}</h4>
          <div class="dc-meta">
            <div class="dc-meta-row">
              <span class="mi">location_on</span>
              <span>{{ device.location }}</span>
            </div>
            <div class="dc-meta-row">
              <span class="mi">settings_ethernet</span>
              <span>{{ device.ip }}</span>
            </div>
          </div>
        </div>
        <div class="dc-footer">
          <button class="act-btn"><span class="mi">settings</span></button>
          <button class="act-btn"><span class="mi">videocam</span></button>
          <button class="act-btn"><span class="mi">more_vert</span></button>
        </div>
      </div>
      <div class="dev-card add-card">
        <span class="mi">add</span>
        <span>Add Device</span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.devices-page {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.device-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 12px;
}

.dt-left {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.dt-right {
  display: flex;
  align-items: center;
  gap: 8px;
}

.filter-chips {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
}

.filter-chip {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  height: 32px;
  padding: 0 14px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  font: 500 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
  background: transparent;
  cursor: pointer;
  transition: all .15s;
  white-space: nowrap;
}

.filter-chip:hover {
  background: var(--sc2);
}

.filter-chip.active {
  background: var(--pri-c);
  color: var(--on-pri-c);
  border-color: var(--pri-c);
}

.filter-chip .chip-count {
  background: rgba(255,255,255,.12);
  padding: 1px 6px;
  border-radius: 10px;
  font-size: 11px;
  margin-left: 2px;
}

.view-toggle {
  display: flex;
  border: 1px solid var(--olv);
  border-radius: var(--r2);
  overflow: hidden;
}

.vt-btn {
  width: 36px;
  height: 34px;
  border: none;
  background: transparent;
  color: var(--on-sfv);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all .15s;
}

.vt-btn:hover {
  background: var(--sc2);
}

.vt-btn.active {
  background: var(--pri-c);
  color: var(--on-pri-c);
}

.vt-btn .mi {
  font-size: 20px;
}

.mt-btn {
  height: 34px;
  padding: 0 14px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
  font: 500 13px/18px 'Roboto', sans-serif;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 6px;
  transition: all .15s;
}

.mt-btn:hover {
  background: var(--sc2);
}

.mt-btn .mi {
  font-size: 18px;
}

.mt-btn.primary {
  background: var(--pri);
  color: var(--on-pri);
  border-color: var(--pri);
}

.device-stats-row {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
}

.alert-stat-mini {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 16px;
  display: flex;
  align-items: center;
  gap: 14px;
}

.asm-icon {
  width: 40px;
  height: 40px;
  border-radius: var(--r2);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.asm-icon .mi {
  font-size: 20px;
}

.asm-info h4 {
  font: 500 20px/26px 'Roboto', sans-serif;
}

.asm-info p {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.device-table-wrap {
  background: var(--sc);
  border-radius: var(--r3);
  overflow: hidden;
}

.device-table {
  width: 100%;
}

.dt-head {
  display: grid;
  grid-template-columns: 40px 1fr 140px 120px 100px 100px 80px;
  align-items: center;
  padding: 12px 18px;
  background: var(--sc2);
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  text-transform: uppercase;
  letter-spacing: .5px;
}

.dt-head .sortable {
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 3px;
}

.dt-head .sortable:hover {
  color: var(--pri);
}

.dt-head .sortable .mi {
  font-size: 14px;
}

.dt-row {
  display: grid;
  grid-template-columns: 40px 1fr 140px 120px 100px 100px 80px;
  align-items: center;
  padding: 12px 18px;
  border-bottom: 1px solid var(--olv);
  transition: background .1s;
  cursor: pointer;
}

.dt-row:hover {
  background: var(--sc2);
}

.dt-row .dt-cb input {
  width: 16px;
  height: 16px;
  accent-color: var(--pri);
}

.dt-row .dt-device {
  display: flex;
  align-items: center;
  gap: 12px;
  min-width: 0;
}

.dt-row .dt-device .dev-thumb {
  width: 48px;
  height: 32px;
  border-radius: var(--r1);
  background: linear-gradient(135deg, #0d0d15, #1c1728);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  overflow: hidden;
  position: relative;
}

.dt-row .dt-device .dev-thumb .mi {
  font-size: 16px;
  color: rgba(255,255,255,.1);
}

.dt-row .dt-device .dev-info {
  min-width: 0;
}

.dt-row .dt-device .dev-info h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.dt-row .dt-device .dev-info p {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.dt-row .dt-cell {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.dt-row .dt-status {
  display: flex;
  align-items: center;
  gap: 6px;
  font: 500 13px/18px 'Roboto', sans-serif;
  text-transform: capitalize;
}

.dt-row .dt-status .dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  flex-shrink: 0;
}

.dt-row .dt-status .dot.on {
  background: var(--green);
}

.dt-row .dt-status .dot.off {
  background: var(--red);
}

.dt-row .dt-status .dot.rec {
  background: var(--orange);
}

.dt-row .dt-actions {
  display: flex;
  gap: 4px;
  justify-content: flex-end;
}

.dt-row .dt-actions .act-btn {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  border: none;
  background: transparent;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: background .15s;
}

.dt-row .dt-actions .act-btn:hover {
  background: var(--sc3);
}

.dt-row .dt-actions .act-btn .mi {
  font-size: 18px;
  color: var(--on-sfv);
}

.device-card-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 14px;
}

.dev-card {
  background: var(--sc);
  border-radius: var(--r3);
  overflow: hidden;
  cursor: pointer;
  transition: transform .15s, box-shadow .15s;
}

.dev-card:hover {
  transform: translateY(-2px);
  box-shadow: var(--e2);
}

.dev-card .dc-thumb {
  width: 100%;
  aspect-ratio: 16/9;
  background: linear-gradient(135deg, #0d0d15, #1c1728);
  position: relative;
  overflow: hidden;
  display: flex;
  align-items: center;
  justify-content: center;
}

.dev-card .dc-thumb .mi {
  font-size: 36px;
  color: rgba(255,255,255,.06);
}

.dev-card .dc-thumb .sv-grid {
  position: absolute;
  inset: 0;
  background: linear-gradient(rgba(255,255,255,.015) 1px, transparent 1px), linear-gradient(90deg, rgba(255,255,255,.015) 1px, transparent 1px);
  background-size: 20px 20px;
}

.dev-card .dc-thumb .dc-status-badge {
  position: absolute;
  top: 8px;
  left: 8px;
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 3px 8px;
  border-radius: var(--r1);
  font: 500 10px/16px 'Roboto', sans-serif;
  color: #fff;
}

.dev-card .dc-thumb .dc-status-badge.online {
  background: rgba(60,160,70,.85);
}

.dev-card .dc-thumb .dc-status-badge.offline {
  background: rgba(160,60,60,.85);
}

.dev-card .dc-thumb .dc-status-badge.recording {
  background: rgba(200,60,60,.9);
}

.dev-card .dc-thumb .dc-status-badge .dot {
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: #fff;
}

.dev-card .dc-body {
  padding: 14px;
}

.dev-card .dc-body h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
  margin-bottom: 4px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.dev-card .dc-meta {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.dev-card .dc-meta-row {
  display: flex;
  align-items: center;
  gap: 6px;
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.dev-card .dc-meta-row .mi {
  font-size: 14px;
  flex-shrink: 0;
  color: var(--ol);
}

.dev-card .dc-footer {
  display: flex;
  justify-content: flex-end;
  gap: 4px;
  padding: 0 14px 12px;
}

.dev-card .dc-footer .act-btn {
  width: 30px;
  height: 30px;
  border-radius: 50%;
  border: none;
  background: var(--sc2);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: background .15s;
}

.dev-card .dc-footer .act-btn:hover {
  background: var(--sc3);
}

.dev-card .dc-footer .act-btn .mi {
  font-size: 16px;
  color: var(--on-sfv);
}

.dev-card.add-card {
  border: 2px dashed var(--olv);
  background: transparent;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  min-height: 220px;
  gap: 10px;
  transition: border-color .15s, background .15s;
}

.dev-card.add-card:hover {
  border-color: var(--pri);
  background: rgba(200,191,255,.03);
}

.dev-card.add-card .mi {
  font-size: 36px;
  color: var(--olv);
}

.dev-card.add-card span {
  font: 500 14px/20px 'Roboto', sans-serif;
  color: var(--ol);
}

@media (max-width: 1439px) {
  .device-stats-row {
    grid-template-columns: repeat(2, 1fr);
  }

  .dt-head, .dt-row {
    grid-template-columns: 40px 1fr 130px 110px 100px 70px;
  }

  .dt-head > :nth-child(5), .dt-row > :nth-child(5),
  .dt-head > :nth-child(6), .dt-row > :nth-child(6) {
    display: none;
  }
}

@media (max-width: 1023px) {
  .device-stats-row {
    grid-template-columns: repeat(2, 1fr);
  }

  .dt-head, .dt-row {
    grid-template-columns: 40px 1fr 100px 70px;
  }
}

@media (max-width: 768px) {
  .device-stats-row {
    grid-template-columns: 1fr;
  }

  .device-card-grid {
    grid-template-columns: 1fr;
  }
}
</style>
