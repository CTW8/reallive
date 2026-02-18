<script setup>
</script>

<template>
  <div class="storage-page">
    <div class="storage-grid">
      <div class="storage-overview">
        <div class="storage-ring">
          <svg viewBox="0 0 100 100">
            <circle class="ring-bg" cx="50" cy="50" r="42"></circle>
            <circle class="ring-fill ring-used" cx="50" cy="50" r="42" stroke-dasharray="198" stroke-dashoffset="50"></circle>
            <circle class="ring-fill ring-video" cx="50" cy="50" r="42" stroke-dasharray="198" stroke-dashoffset="90"></circle>
            <circle class="ring-fill ring-snap" cx="50" cy="50" r="42" stroke-dasharray="198" stroke-dashoffset="150"></circle>
            <circle class="ring-fill ring-cache" cx="50" cy="50" r="42" stroke-dasharray="198" stroke-dashoffset="180"></circle>
          </svg>
          <div class="ring-center">
            <span class="ring-pct">68%</span>
            <span class="ring-label">Used</span>
          </div>
        </div>
        <div class="storage-detail">
          <h3>Storage Overview</h3>
          <p class="sd-sub">2.1 TB of 3.0 TB used</p>
          <div class="storage-breakdown">
            <div class="sb-item">
              <span class="sb-dot" style="background:var(--pri)"></span>
              <div class="sb-info">
                <div class="sb-name"><span>Video Recordings</span><span>1.4 TB</span></div>
                <div class="sb-bar"><div class="sb-fill" style="width:67%;background:var(--pri)"></div></div>
              </div>
            </div>
            <div class="sb-item">
              <span class="sb-dot" style="background:var(--ter)"></span>
              <div class="sb-info">
                <div class="sb-name"><span>Snapshots</span><span>420 GB</span></div>
                <div class="sb-bar"><div class="sb-fill" style="width:20%;background:var(--ter)"></div></div>
              </div>
            </div>
            <div class="sb-item">
              <span class="sb-dot" style="background:var(--orange)"></span>
              <div class="sb-info">
                <div class="sb-name"><span>Cache</span><span>180 GB</span></div>
                <div class="sb-bar"><div class="sb-fill" style="width:9%;background:var(--orange)"></div></div>
              </div>
            </div>
            <div class="sb-item">
              <span class="sb-dot" style="background:var(--red)"></span>
              <div class="sb-info">
                <div class="sb-name"><span>Other</span><span>100 GB</span></div>
                <div class="sb-bar"><div class="sb-fill" style="width:5%;background:var(--red)"></div></div>
              </div>
            </div>
          </div>
        </div>
      </div>

      <div class="storage-trend">
        <h3>Storage Trend</h3>
        <p class="st-sub">Last 14 days usage</p>
        <div class="trend-chart">
          <div class="tc-bar" style="height:45%"></div>
          <div class="tc-bar" style="height:52%"></div>
          <div class="tc-bar" style="height:48%"></div>
          <div class="tc-bar" style="height:55%"></div>
          <div class="tc-bar" style="height:60%"></div>
          <div class="tc-bar" style="height:58%"></div>
          <div class="tc-bar" style="height:62%"></div>
          <div class="tc-bar" style="height:65%"></div>
          <div class="tc-bar" style="height:63%"></div>
          <div class="tc-bar" style="height:66%"></div>
          <div class="tc-bar" style="height:68%"></div>
          <div class="tc-bar" style="height:67%"></div>
          <div class="tc-bar" style="height:68%"></div>
          <div class="tc-bar" style="height:68%"></div>
        </div>
        <div class="trend-labels">
          <span>Feb 5</span>
          <span>Feb 12</span>
          <span>Feb 18</span>
        </div>
        <div class="trend-predict">
          <span class="mi">info</span>
          <span>Estimated full in <strong>12 days</strong> at current rate</span>
        </div>
      </div>
    </div>

    <div class="storage-device-table">
      <h3>Storage by Device</h3>
      <div class="sdt-row header">
        <div>Device</div>
        <div>Videos</div>
        <div>Snapshots</div>
        <div>Usage</div>
        <div></div>
      </div>
      <div class="sdt-row">
        <div class="sdt-device">
          <span class="mi">videocam</span>
          <span>Front Door Camera</span>
        </div>
        <div class="sdt-cell">450 GB</div>
        <div class="sdt-cell">120 GB</div>
        <div class="sdt-bar">
          <div class="bar-track"><div class="bar-fill" style="width:57%;background:var(--pri)"></div></div>
          <span class="bar-pct">57%</span>
        </div>
        <div class="sdt-actions"><button><span class="mi">delete_sweep</span> Clear</button></div>
      </div>
      <div class="sdt-row">
        <div class="sdt-device">
          <span class="mi">videocam</span>
          <span>Parking Lot B2</span>
        </div>
        <div class="sdt-cell">380 GB</div>
        <div class="sdt-cell">85 GB</div>
        <div class="sdt-bar">
          <div class="bar-track"><div class="bar-fill" style="width:48%;background:var(--pri)"></div></div>
          <span class="bar-pct">48%</span>
        </div>
        <div class="sdt-actions"><button><span class="mi">delete_sweep</span> Clear</button></div>
      </div>
      <div class="sdt-row">
        <div class="sdt-device">
          <span class="mi">videocam</span>
          <span>Lobby - Reception</span>
        </div>
        <div class="sdt-cell">320 GB</div>
        <div class="sdt-cell">95 GB</div>
        <div class="sdt-bar">
          <div class="bar-track"><div class="bar-fill" style="width:42%;background:var(--pri)"></div></div>
          <span class="bar-pct">42%</span>
        </div>
        <div class="sdt-actions"><button><span class="mi">delete_sweep</span> Clear</button></div>
      </div>
    </div>

    <div class="storage-policy">
      <h3>Storage Policies</h3>
      <div class="policy-grid">
        <div class="policy-card">
          <div class="pc-top">
            <h4><span class="mi">schedule</span> Auto Retention</h4>
            <div class="sw-web on"></div>
          </div>
          <p class="pc-desc">Automatically delete old recordings</p>
          <div class="pc-value">
            <select>
              <option>7 days</option>
              <option selected>14 days</option>
              <option>30 days</option>
              <option>90 days</option>
            </select>
          </div>
        </div>
        <div class="policy-card">
          <div class="pc-top">
            <h4><span class="mi">cloud_upload</span> Cloud Backup</h4>
            <div class="sw-web"></div>
          </div>
          <p class="pc-desc">Backup recordings to cloud storage</p>
          <div class="pc-value">
            <select disabled>
              <option>Enable to configure</option>
            </select>
          </div>
        </div>
        <div class="policy-card">
          <div class="pc-top">
            <h4><span class="mi">photo_camera</span> Snapshot Retention</h4>
            <div class="sw-web on"></div>
          </div>
          <p class="pc-desc">Auto delete old snapshots</p>
          <div class="pc-value">
            <select>
              <option>3 days</option>
              <option selected>7 days</option>
              <option>14 days</option>
            </select>
          </div>
        </div>
        <div class="policy-card">
          <div class="pc-top">
            <h4><span class="mi">delete_sweep</span> Cache Cleanup</h4>
            <div class="sw-web on"></div>
          </div>
          <p class="pc-desc">Clear cache when exceeds limit</p>
          <div class="pc-value">
            <select>
              <option>1 GB</option>
              <option selected>2 GB</option>
              <option>5 GB</option>
            </select>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.storage-page {
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.storage-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
}

.storage-overview {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 24px;
  display: flex;
  align-items: center;
  gap: 32px;
}

.storage-ring {
  position: relative;
  width: 160px;
  height: 160px;
  flex-shrink: 0;
}

.storage-ring svg {
  width: 100%;
  height: 100%;
  transform: rotate(-90deg);
}

.storage-ring .ring-bg {
  fill: none;
  stroke: var(--sc2);
  stroke-width: 14;
}

.storage-ring .ring-fill {
  fill: none;
  stroke-width: 14;
  stroke-linecap: round;
  transition: stroke-dashoffset .8s ease;
}

.ring-center {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
}

.ring-pct {
  font: 500 32px/40px 'Roboto', sans-serif;
}

.ring-label {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.storage-detail {
  flex: 1;
}

.storage-detail h3 {
  font: 500 16px/24px 'Roboto', sans-serif;
  margin-bottom: 4px;
}

.sd-sub {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
  margin-bottom: 16px;
}

.storage-breakdown {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.sb-item {
  display: flex;
  align-items: center;
  gap: 12px;
}

.sb-item .sb-dot {
  width: 10px;
  height: 10px;
  border-radius: 3px;
  flex-shrink: 0;
}

.sb-item .sb-info {
  flex: 1;
  min-width: 0;
}

.sb-item .sb-info .sb-name {
  font: 400 13px/18px 'Roboto', sans-serif;
  display: flex;
  justify-content: space-between;
  margin-bottom: 4px;
}

.sb-item .sb-info .sb-name span:last-child {
  color: var(--on-sfv);
}

.sb-item .sb-bar {
  width: 100%;
  height: 4px;
  border-radius: 2px;
  background: var(--sc2);
  overflow: hidden;
}

.sb-item .sb-bar .sb-fill {
  height: 100%;
  border-radius: 2px;
}

.storage-trend {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 24px;
}

.storage-trend h3 {
  font: 500 16px/24px 'Roboto', sans-serif;
  margin-bottom: 4px;
}

.st-sub {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
  margin-bottom: 16px;
}

.trend-chart {
  height: 140px;
  display: flex;
  align-items: flex-end;
  gap: 3px;
  padding-bottom: 24px;
  position: relative;
  border-bottom: 1px solid var(--olv);
}

.trend-chart .tc-bar {
  flex: 1;
  border-radius: var(--r1) var(--r1) 0 0;
  transition: height .3s;
  position: relative;
  min-height: 4px;
  cursor: pointer;
  background: var(--pri);
  opacity: .7;
}

.trend-chart .tc-bar:hover {
  opacity: 1;
}

.trend-labels {
  display: flex;
  justify-content: space-between;
  margin-top: 6px;
}

.trend-labels span {
  font: 400 10px/14px 'Roboto', sans-serif;
  color: var(--ol);
}

.trend-predict {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-top: 16px;
  padding: 10px 14px;
  border-radius: var(--r2);
  background: rgba(255,158,67,.08);
  border: 1px solid rgba(255,158,67,.2);
}

.trend-predict .mi {
  font-size: 18px;
  color: var(--orange);
}

.trend-predict span {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.trend-predict strong {
  color: var(--orange);
}

.storage-device-table {
  background: var(--sc);
  border-radius: var(--r3);
  overflow: hidden;
}

.storage-device-table h3 {
  font: 500 15px/22px 'Roboto', sans-serif;
  padding: 16px 18px;
  border-bottom: 1px solid var(--olv);
}

.sdt-row {
  display: grid;
  grid-template-columns: 1fr 120px 120px 1fr 100px;
  align-items: center;
  padding: 12px 18px;
  border-bottom: 1px solid var(--olv);
  gap: 12px;
}

.sdt-row:last-child {
  border-bottom: none;
}

.sdt-row.header {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  text-transform: uppercase;
  letter-spacing: .5px;
  background: var(--sc2);
}

.sdt-row .sdt-device {
  display: flex;
  align-items: center;
  gap: 10px;
  min-width: 0;
}

.sdt-row .sdt-device .mi {
  font-size: 18px;
  color: var(--on-sfv);
}

.sdt-row .sdt-device span {
  font: 400 14px/20px 'Roboto', sans-serif;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.sdt-row .sdt-cell {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.sdt-row .sdt-bar {
  display: flex;
  align-items: center;
  gap: 8px;
}

.sdt-row .sdt-bar .bar-track {
  flex: 1;
  height: 6px;
  border-radius: 3px;
  background: var(--sc2);
  overflow: hidden;
}

.sdt-row .sdt-bar .bar-fill {
  height: 100%;
  border-radius: 3px;
}

.sdt-row .sdt-bar .bar-pct {
  font: 500 12px/16px 'Roboto', sans-serif;
  min-width: 36px;
  text-align: right;
}

.sdt-row .sdt-actions button {
  height: 28px;
  padding: 0 10px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
  font: 400 12px/16px 'Roboto', sans-serif;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 4px;
  transition: all .15s;
}

.sdt-row .sdt-actions button:hover {
  background: var(--sc2);
}

.sdt-row .sdt-actions button .mi {
  font-size: 14px;
}

.storage-policy {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 20px;
}

.storage-policy h3 {
  font: 500 15px/22px 'Roboto', sans-serif;
  margin-bottom: 16px;
}

.policy-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
  gap: 14px;
}

.policy-card {
  background: var(--sc2);
  border-radius: var(--r2);
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.policy-card .pc-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.policy-card .pc-top h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
  display: flex;
  align-items: center;
  gap: 8px;
}

.policy-card .pc-top h4 .mi {
  font-size: 20px;
  color: var(--pri);
}

.sw-web {
  width: 42px;
  height: 24px;
  border-radius: 12px;
  background: var(--sfv);
  position: relative;
  cursor: pointer;
  border: none;
  flex-shrink: 0;
  transition: background .2s;
}

.sw-web.on {
  background: var(--pri);
}

.sw-web::after {
  content: '';
  position: absolute;
  top: 3px;
  left: 3px;
  width: 18px;
  height: 18px;
  border-radius: 50%;
  background: var(--ol);
  transition: all .2s;
}

.sw-web.on::after {
  left: 21px;
  background: var(--on-pri);
}

.policy-card .pc-desc {
  font: 400 12px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.policy-card .pc-value {
  display: flex;
  align-items: center;
  gap: 8px;
}

.policy-card .pc-value select {
  height: 32px;
  padding: 0 8px;
  border-radius: var(--r2);
  border: 1px solid var(--olv);
  background: var(--sc);
  color: var(--on-sf);
  font: 400 13px/18px 'Roboto', sans-serif;
  outline: none;
  cursor: pointer;
  flex: 1;
}

.policy-card .pc-value select:disabled {
  opacity: .5;
  cursor: not-allowed;
}

@media (max-width: 1439px) {
  .storage-grid {
    grid-template-columns: 1fr;
  }

  .sdt-row {
    grid-template-columns: 1fr 100px 100px 1fr 80px;
  }

  .sdt-row .sdt-cell:nth-child(3) {
    display: none;
  }
}

@media (max-width: 768px) {
  .storage-overview {
    flex-direction: column;
    text-align: center;
  }

  .policy-grid {
    grid-template-columns: 1fr;
  }
}
</style>
