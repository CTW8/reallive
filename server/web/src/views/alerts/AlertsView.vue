<script setup>
</script>

<template>
  <div class="alerts-page">
    <div class="alert-toolbar">
      <div class="at-left">
        <div class="filter-chips">
          <button class="filter-chip active">All <span class="chip-count">24</span></button>
          <button class="filter-chip">Motion <span class="chip-count">12</span></button>
          <button class="filter-chip">Alarm <span class="chip-count">5</span></button>
          <button class="filter-chip">Offline <span class="chip-count">3</span></button>
          <button class="filter-chip">System <span class="chip-count">4</span></button>
        </div>
      </div>
      <div class="at-right">
        <button class="mt-btn"><span class="mi">filter_list</span> Filter</button>
        <button class="mt-btn"><span class="mi">download</span> Export</button>
      </div>
    </div>

    <div class="alert-stats-row">
      <div class="alert-stat-mini">
        <div class="asm-icon" style="background:rgba(255,107,107,.12);color:var(--red)">
          <span class="mi">warning</span>
        </div>
        <div class="asm-info">
          <h4>5</h4>
          <p>New Alerts</p>
        </div>
      </div>
      <div class="alert-stat-mini">
        <div class="asm-icon" style="background:rgba(255,158,67,.12);color:var(--orange)">
          <span class="mi">visibility</span>
        </div>
        <div class="asm-info">
          <h4>12</h4>
          <p>Read</p>
        </div>
      </div>
      <div class="alert-stat-mini">
        <div class="asm-icon" style="background:rgba(125,216,129,.12);color:var(--green)">
          <span class="mi">check_circle</span>
        </div>
        <div class="asm-info">
          <h4>8</h4>
          <p>Resolved</p>
        </div>
      </div>
      <div class="alert-stat-mini">
        <div class="asm-icon" style="background:rgba(200,191,255,.12);color:var(--pri)">
          <span class="mi">schedule</span>
        </div>
        <div class="asm-info">
          <h4>2m</h4>
          <p>Avg Response</p>
        </div>
      </div>
    </div>

    <div class="alert-table-wrap">
      <div class="alert-table-header">
        <div class="ath-left">
          <label class="select-all">
            <input type="checkbox">
            <span>Select all</span>
          </label>
        </div>
        <div class="batch-actions">
          <button class="batch-btn"><span class="mi">check</span> Mark Read</button>
          <button class="batch-btn danger"><span class="mi">delete</span> Delete</button>
        </div>
      </div>
      <div class="alert-table">
        <div class="at-row header">
          <div class="at-cb"></div>
          <div>Alert</div>
          <div>Camera</div>
          <div>Time</div>
          <div>Status</div>
          <div></div>
        </div>
        <div class="at-row unread">
          <div class="at-cb"><input type="checkbox"></div>
          <div class="at-main">
            <div class="at-icon motion"><span class="mi">directions_run</span></div>
            <div class="at-text">
              <h4>Motion Detected</h4>
              <p>Unusual activity in restricted zone</p>
            </div>
          </div>
          <div class="at-camera">Front Door Camera</div>
          <div class="at-time">2 min ago</div>
          <div class="at-status"><span class="status-tag new">New</span></div>
          <div class="at-actions">
            <button class="act-btn"><span class="mi">visibility</span></button>
            <button class="act-btn"><span class="mi">check</span></button>
          </div>
        </div>
        <div class="at-row unread">
          <div class="at-cb"><input type="checkbox"></div>
          <div class="at-main">
            <div class="at-icon alarm"><span class="mi">warning</span></div>
            <div class="at-text">
              <h4>Camera Offline</h4>
              <p>Connection lost, last seen 14:20:33</p>
            </div>
          </div>
          <div class="at-camera">Warehouse East</div>
          <div class="at-time">5 min ago</div>
          <div class="at-status"><span class="status-tag new">New</span></div>
          <div class="at-actions">
            <button class="act-btn"><span class="mi">visibility</span></button>
            <button class="act-btn"><span class="mi">check</span></button>
          </div>
        </div>
        <div class="at-row">
          <div class="at-cb"><input type="checkbox"></div>
          <div class="at-main">
            <div class="at-icon motion"><span class="mi">directions_run</span></div>
            <div class="at-text">
              <h4>Motion Detected</h4>
              <p>Vehicle movement after hours</p>
            </div>
          </div>
          <div class="at-camera">Parking Lot B2</div>
          <div class="at-time">12 min ago</div>
          <div class="at-status"><span class="status-tag read">Read</span></div>
          <div class="at-actions">
            <button class="act-btn"><span class="mi">visibility</span></button>
            <button class="act-btn"><span class="mi">check</span></button>
          </div>
        </div>
        <div class="at-row">
          <div class="at-cb"><input type="checkbox"></div>
          <div class="at-main">
            <div class="at-icon info"><span class="mi">info</span></div>
            <div class="at-text">
              <h4>Storage Warning</h4>
              <p>Cloud storage at 85% capacity</p>
            </div>
          </div>
          <div class="at-camera">System</div>
          <div class="at-time">28 min ago</div>
          <div class="at-status"><span class="status-tag resolved">Resolved</span></div>
          <div class="at-actions">
            <button class="act-btn"><span class="mi">visibility</span></button>
            <button class="act-btn"><span class="mi">check</span></button>
          </div>
        </div>
      </div>
      <div class="table-pagination">
        <span class="tp-info">Showing 1-5 of 24 alerts</span>
        <div class="tp-controls">
          <button class="page-btn disabled"><span class="mi">chevron_left</span></button>
          <button class="page-btn active">1</button>
          <button class="page-btn">2</button>
          <button class="page-btn">3</button>
          <button class="page-btn"><span class="mi">chevron_right</span></button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.alerts-page {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.alert-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 12px;
}

.at-left {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.at-right {
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

.filter-chip.active .chip-count {
  background: rgba(255,255,255,.2);
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

.alert-stats-row {
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

.alert-table-wrap {
  background: var(--sc);
  border-radius: var(--r3);
  overflow: hidden;
}

.alert-table-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 18px;
  border-bottom: 1px solid var(--olv);
}

.ath-left {
  display: flex;
  align-items: center;
  gap: 12px;
}

.select-all {
  display: flex;
  align-items: center;
  gap: 8px;
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
  cursor: pointer;
}

.select-all input {
  width: 16px;
  height: 16px;
  accent-color: var(--pri);
}

.batch-actions {
  display: flex;
  gap: 6px;
}

.batch-btn {
  height: 30px;
  padding: 0 12px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
  font: 500 12px/16px 'Roboto', sans-serif;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 4px;
  transition: all .15s;
}

.batch-btn:hover {
  background: var(--sc2);
}

.batch-btn .mi {
  font-size: 15px;
}

.batch-btn.danger:hover {
  background: rgba(255,107,107,.12);
  color: var(--red);
  border-color: var(--red);
}

.alert-table {
  width: 100%;
}

.alert-table .at-row {
  display: grid;
  grid-template-columns: 40px 1fr 140px 140px 120px 60px;
  align-items: center;
  padding: 12px 18px;
  border-bottom: 1px solid var(--olv);
  transition: background .1s;
  cursor: pointer;
}

.alert-table .at-row:hover {
  background: var(--sc2);
}

.alert-table .at-row.unread {
  background: rgba(200,191,255,.03);
}

.alert-table .at-row.header {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  text-transform: uppercase;
  letter-spacing: .5px;
  cursor: default;
}

.alert-table .at-row.header:hover {
  background: transparent;
}

.at-row .at-cb {
  display: flex;
  align-items: center;
}

.at-row .at-cb input {
  width: 16px;
  height: 16px;
  accent-color: var(--pri);
}

.at-row .at-main {
  display: flex;
  align-items: center;
  gap: 12px;
  min-width: 0;
}

.at-row .at-icon {
  width: 34px;
  height: 34px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.at-row .at-icon .mi {
  font-size: 18px;
}

.at-row .at-icon.motion {
  background: rgba(255,158,67,.12);
  color: var(--orange);
}

.at-row .at-icon.alarm {
  background: rgba(255,107,107,.12);
  color: var(--red);
}

.at-row .at-icon.info {
  background: rgba(200,191,255,.12);
  color: var(--pri);
}

.at-row .at-text {
  min-width: 0;
}

.at-row .at-text h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.at-row .at-text p {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.at-row .at-camera {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.at-row .at-time {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--ol);
}

.at-row .at-status {
  display: flex;
  align-items: center;
}

.at-row .at-status .status-tag {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 3px 10px;
  border-radius: var(--r6);
  font: 500 11px/16px 'Roboto', sans-serif;
}

.status-tag.new {
  background: rgba(255,107,107,.12);
  color: var(--red);
}

.status-tag.read {
  background: rgba(147,143,153,.15);
  color: var(--ol);
}

.status-tag.resolved {
  background: rgba(125,216,129,.12);
  color: var(--green);
}

.at-row .at-actions {
  display: flex;
  gap: 4px;
  justify-content: flex-end;
}

.at-row .at-actions .act-btn {
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

.at-row .at-actions .act-btn:hover {
  background: var(--sc3);
}

.at-row .at-actions .act-btn .mi {
  font-size: 18px;
  color: var(--on-sfv);
}

.table-pagination {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 18px;
}

.tp-info {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.tp-controls {
  display: flex;
  align-items: center;
  gap: 4px;
}

.page-btn {
  width: 32px;
  height: 32px;
  border-radius: var(--r2);
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
  font: 500 13px/18px 'Roboto', sans-serif;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all .15s;
}

.page-btn:hover {
  background: var(--sc2);
}

.page-btn.active {
  background: var(--pri-c);
  color: var(--on-pri-c);
  border-color: var(--pri-c);
}

.page-btn.disabled {
  opacity: .3;
  cursor: default;
  pointer-events: none;
}

.page-btn .mi {
  font-size: 18px;
}

@media (max-width: 1439px) {
  .alert-stats-row {
    grid-template-columns: repeat(2, 1fr);
  }

  .alert-table .at-row {
    grid-template-columns: 40px 1fr 130px 130px 100px 50px;
  }

  .alert-table .at-row .at-status {
    display: none;
  }
}

@media (max-width: 1023px) {
  .alert-stats-row {
    grid-template-columns: repeat(2, 1fr);
  }

  .alert-table .at-row {
    grid-template-columns: 40px 1fr 120px 50px;
  }

  .alert-table .at-row .at-camera,
  .alert-table .at-row .at-status {
    display: none;
  }
}

@media (max-width: 768px) {
  .alert-stats-row {
    grid-template-columns: 1fr;
  }
}
</style>
