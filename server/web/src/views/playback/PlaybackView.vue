<script setup>
</script>

<template>
  <div class="playback-page">
    <div class="pb-layout">
      <div class="pb-sidebar">
        <div class="pbs-header">
          <div class="pbs-title-row">
            <h3>Date & Cameras</h3>
            <span class="pbs-selected"><span class="mi">videocam</span> <strong>2</strong> selected</span>
          </div>
          <p class="pbs-sub">Select date and cameras to playback</p>
        </div>
        <div class="pbs-section pbs-date-section">
          <div class="pbs-section-head"><span class="mi">calendar_today</span> Date</div>
          <div class="mini-cal">
            <div class="mc-nav">
              <span>February 2026</span>
              <div>
                <button><span class="mi">chevron_left</span></button>
                <button><span class="mi">chevron_right</span></button>
              </div>
            </div>
            <div class="mc-grid">
              <span class="mc-day">S</span>
              <span class="mc-day">M</span>
              <span class="mc-day">T</span>
              <span class="mc-day">W</span>
              <span class="mc-day">T</span>
              <span class="mc-day">F</span>
              <span class="mc-day">S</span>
              <span class="mc-date dim">26</span>
              <span class="mc-date dim">27</span>
              <span class="mc-date dim">28</span>
              <span class="mc-date dim">29</span>
              <span class="mc-date dim">30</span>
              <span class="mc-date dim">31</span>
              <span class="mc-date">1</span>
              <span class="mc-date">2</span>
              <span class="mc-date">3</span>
              <span class="mc-date">4</span>
              <span class="mc-date">5</span>
              <span class="mc-date">6</span>
              <span class="mc-date">7</span>
              <span class="mc-date">8</span>
              <span class="mc-date today">9</span>
              <span class="mc-date">10</span>
              <span class="mc-date has-event">11</span>
              <span class="mc-date">12</span>
              <span class="mc-date">13</span>
              <span class="mc-date">14</span>
              <span class="mc-date">15</span>
            </div>
          </div>
        </div>
        <div class="pbs-section pbs-camera-section">
          <div class="pbs-section-head"><span class="mi">videocam</span> Cameras</div>
          <div class="pbs-cam-top">
            <button class="pbs-cam-all">Select All</button>
            <button class="pbs-cam-all">Clear</button>
          </div>
          <div class="pbs-cam-search">
            <span class="mi">search</span>
            <input type="text" placeholder="Search cameras...">
          </div>
          <div class="pbs-cameras">
            <div class="pbs-cam-item selected">
              <input type="checkbox" checked>
              <span class="pci-dot" style="background:var(--green)"></span>
              <span class="pci-name">Front Door Camera</span>
            </div>
            <div class="pbs-cam-item selected">
              <input type="checkbox" checked>
              <span class="pci-dot" style="background:var(--orange)"></span>
              <span class="pci-name">Parking Lot B2</span>
            </div>
            <div class="pbs-cam-item">
              <input type="checkbox">
              <span class="pci-dot" style="background:var(--green)"></span>
              <span class="pci-name">Lobby - Reception</span>
            </div>
          </div>
        </div>
      </div>
      <div class="pb-main">
        <div class="pb-video-area quad">
          <div class="pb-video-cell">
            <div class="sv-grid"></div>
            <div class="sv-icon"><span class="mi">videocam</span></div>
            <div class="pv-top">
              <span class="cam-name">Front Door</span>
              <span class="pb-speed">1.0x</span>
            </div>
            <div class="pv-center">
              <button class="play-btn"><span class="mi">play_arrow</span></button>
            </div>
          </div>
          <div class="pb-video-cell">
            <div class="sv-grid"></div>
            <div class="sv-icon"><span class="mi">videocam</span></div>
            <div class="pv-top">
              <span class="cam-name">Parking Lot</span>
              <span class="pb-speed">1.0x</span>
            </div>
            <div class="pv-center">
              <button class="play-btn"><span class="mi">play_arrow</span></button>
            </div>
          </div>
        </div>
        <div class="pb-controls">
          <div class="pb-controls-top">
            <div class="pbc-left">
              <button class="pbc-btn"><span class="mi">skip_previous</span></button>
              <button class="pbc-btn play"><span class="mi">play_arrow</span></button>
              <button class="pbc-btn"><span class="mi">skip_next</span></button>
            </div>
            <span class="pbc-time">14:20:35 / 14:30:00</span>
            <div class="pbc-right">
              <select class="speed-select">
                <option>0.5x</option>
                <option selected>1.0x</option>
                <option>1.5x</option>
                <option>2.0x</option>
              </select>
              <button class="pb-export-btn"><span class="mi">download</span> Export</button>
            </div>
          </div>
          <div class="pb-timeline">
            <span class="pb-tl-start">14:00</span>
            <div class="pb-tl-bar">
              <div class="tl-segment normal" style="left:0;width:30%"></div>
              <div class="tl-segment motion" style="left:35%;width:10%"></div>
              <div class="tl-segment normal" style="left:50%;width:25%"></div>
              <div class="tl-segment alert" style="left:80%;width:8%"></div>
              <div class="tl-cursor" style="left:35%"></div>
            </div>
            <span class="pb-tl-end">15:00</span>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.playback-page {
  height: calc(100vh - var(--topbar-h) - 48px);
}

.pb-layout {
  display: grid;
  grid-template-columns: 300px 1fr;
  gap: 16px;
  height: 100%;
}

.pb-sidebar {
  background: var(--sc);
  border-radius: var(--r3);
  display: flex;
  flex-direction: column;
  overflow: hidden;
  padding: 12px;
  gap: 10px;
}

.pbs-header {
  padding: 12px;
  border: 1px solid var(--olv);
  border-radius: var(--r2);
  background: var(--sc2);
  flex-shrink: 0;
}

.pbs-title-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  margin-bottom: 4px;
}

.pbs-header h3 {
  font: 500 15px/22px 'Roboto', sans-serif;
  margin: 0;
}

.pbs-selected {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 2px 8px;
  border-radius: 999px;
  background: var(--pri-c);
  color: var(--on-pri-c);
  font: 500 11px/16px 'Roboto', sans-serif;
  white-space: nowrap;
}

.pbs-selected .mi {
  font-size: 14px;
}

.pbs-sub {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  margin: 0;
}

.pbs-section {
  border: 1px solid var(--olv);
  border-radius: var(--r2);
  background: var(--sc2);
}

.pbs-section-head {
  display: flex;
  align-items: center;
  gap: 5px;
  font: 500 11px/16px 'Roboto', sans-serif;
  color: var(--ol);
  text-transform: uppercase;
  letter-spacing: .5px;
}

.pbs-section-head .mi {
  font-size: 14px;
}

.pbs-date-section {
  padding: 12px;
  flex-shrink: 0;
}

.pbs-camera-section {
  display: flex;
  flex-direction: column;
  min-height: 0;
  padding: 10px;
}

.pbs-cam-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  padding: 0 2px 8px;
}

.pbs-cam-all {
  height: 24px;
  padding: 0 8px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: transparent;
  font: 500 11px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  cursor: pointer;
}

.pbs-cam-all:hover {
  background: var(--sc3);
}

.pbs-cam-search {
  display: flex;
  align-items: center;
  gap: 6px;
  height: 32px;
  padding: 0 10px;
  border: 1px solid var(--olv);
  border-radius: var(--r2);
  background: var(--sc);
  margin-bottom: 8px;
}

.pbs-cam-search .mi {
  font-size: 16px;
  color: var(--ol);
}

.pbs-cam-search input {
  flex: 1;
  height: 100%;
  border: none;
  background: transparent;
  outline: none;
  color: var(--on-sf);
  font: 400 12px/16px 'Roboto', sans-serif;
}

.mini-cal .mc-nav {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 10px;
}

.mini-cal .mc-nav span {
  font: 500 13px/18px 'Roboto', sans-serif;
}

.mini-cal .mc-nav button {
  width: 28px;
  height: 28px;
  border: none;
  border-radius: 50%;
  background: transparent;
  color: var(--on-sfv);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
}

.mini-cal .mc-nav button:hover {
  background: var(--sc2);
}

.mc-grid {
  display: grid;
  grid-template-columns: repeat(7, 1fr);
  text-align: center;
  gap: 2px;
}

.mc-grid .mc-day {
  font: 500 10px/14px 'Roboto', sans-serif;
  color: var(--ol);
  padding: 4px 0;
}

.mc-grid .mc-date {
  font: 400 12px/16px 'Roboto', sans-serif;
  padding: 4px 0;
  border-radius: 50%;
  cursor: pointer;
  width: 30px;
  height: 30px;
  display: flex;
  align-items: center;
  justify-content: center;
  margin: 0 auto;
  position: relative;
}

.mc-grid .mc-date:hover {
  background: var(--sc2);
}

.mc-grid .mc-date.today {
  background: var(--pri-c);
  color: var(--on-pri-c);
  font-weight: 500;
}

.mc-grid .mc-date.selected {
  background: var(--pri);
  color: var(--on-pri);
  font-weight: 500;
}

.mc-grid .mc-date.dim {
  color: var(--olv);
}

.mc-grid .mc-date.has-event::after {
  content: '';
  position: absolute;
  bottom: 1px;
  left: 50%;
  transform: translateX(-50%);
  width: 4px;
  height: 4px;
  border-radius: 50%;
  background: var(--orange);
}

.pbs-cameras {
  flex: 1;
  overflow-y: auto;
  padding: 4px;
  background: var(--sc);
  border-radius: var(--r2);
  scrollbar-width: none;
}

.pbs-cameras::-webkit-scrollbar {
  display: none;
}

.pbs-cam-item {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 9px 10px;
  border-radius: var(--r2);
  cursor: pointer;
  transition: background .15s;
  margin-bottom: 2px;
}

.pbs-cam-item:hover {
  background: var(--sc2);
}

.pbs-cam-item.selected {
  background: var(--pri-c);
}

.pbs-cam-item input[type="checkbox"] {
  width: 15px;
  height: 15px;
  accent-color: var(--pri);
  flex-shrink: 0;
}

.pbs-cam-item .pci-name {
  font: 400 13px/18px 'Roboto', sans-serif;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  flex: 1;
  min-width: 0;
}

.pb-main {
  display: flex;
  flex-direction: column;
  min-height: 0;
}

.pb-video-area {
  flex: 1;
  display: grid;
  gap: 4px;
  min-height: 0;
  border-radius: var(--r2);
  overflow: hidden;
}

.pb-video-area.quad {
  grid-template-columns: 1fr 1fr;
  grid-template-rows: 1fr 1fr;
}

.pb-video-cell {
  position: relative;
  background: linear-gradient(135deg, #0d0d15, #1c1728);
  overflow: hidden;
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 0;
}

.pb-video-cell .sv-grid {
  position: absolute;
  inset: 0;
  background: linear-gradient(rgba(255,255,255,.015) 1px, transparent 1px), linear-gradient(90deg, rgba(255,255,255,.015) 1px, transparent 1px);
  background-size: 20px 20px;
  pointer-events: none;
}

.pb-video-cell .sv-icon {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
}

.pb-video-cell .sv-icon .mi {
  font-size: 48px;
  color: rgba(255,255,255,.04);
}

.pb-video-cell .pv-top {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  padding: 10px 12px;
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  background: linear-gradient(to bottom, rgba(0,0,0,.6), transparent);
  z-index: 3;
}

.pb-video-cell .pv-top .cam-name {
  font: 500 13px/18px 'Roboto', sans-serif;
  color: #fff;
}

.pb-video-cell .pv-top .pb-speed {
  padding: 3px 8px;
  border-radius: var(--r1);
  background: rgba(200,191,255,.25);
  font: 500 11px/16px 'Roboto', sans-serif;
  color: var(--pri);
}

.pb-video-cell .pv-center {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 2;
}

.pb-video-cell .pv-center .play-btn {
  width: 56px;
  height: 56px;
  border-radius: 50%;
  background: rgba(255,255,255,.1);
  backdrop-filter: blur(8px);
  border: 2px solid rgba(255,255,255,.2);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: background .15s;
}

.pb-video-cell .pv-center .play-btn:hover {
  background: rgba(255,255,255,.2);
}

.pb-video-cell .pv-center .play-btn .mi {
  font-size: 32px;
  color: #fff;
}

.pb-controls {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 14px 18px;
  margin-top: 12px;
  flex-shrink: 0;
}

.pb-controls-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 12px;
  gap: 12px;
}

.pbc-left, .pbc-center, .pbc-right {
  display: flex;
  align-items: center;
  gap: 6px;
}

.pbc-btn {
  width: 34px;
  height: 34px;
  border-radius: 50%;
  border: none;
  background: transparent;
  color: var(--on-sfv);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: background .15s;
}

.pbc-btn:hover {
  background: var(--sc2);
}

.pbc-btn .mi {
  font-size: 22px;
}

.pbc-btn.play {
  background: var(--pri);
  color: var(--on-pri);
}

.pbc-btn.play:hover {
  opacity: .9;
  background: var(--pri);
}

.pbc-btn.play .mi {
  font-size: 24px;
}

.pbc-time {
  font: 500 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
  min-width: 110px;
  text-align: center;
}

.speed-select {
  height: 30px;
  padding: 0 8px;
  border-radius: var(--r2);
  border: 1px solid var(--olv);
  background: var(--sc2);
  color: var(--on-sf);
  font: 500 12px/16px 'Roboto', sans-serif;
  outline: none;
  cursor: pointer;
}

.pb-export-btn {
  height: 32px;
  padding: 0 14px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
  font: 500 12px/16px 'Roboto', sans-serif;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 5px;
  transition: all .15s;
  white-space: nowrap;
}

.pb-export-btn:hover {
  background: var(--sc2);
}

.pb-export-btn .mi {
  font-size: 16px;
}

.pb-timeline {
  position: relative;
  height: 40px;
  margin-top: 4px;
}

.pb-tl-bar {
  position: absolute;
  top: 12px;
  left: 60px;
  right: 60px;
  height: 16px;
  background: var(--sc2);
  border-radius: 3px;
  overflow: hidden;
}

.pb-tl-bar .tl-segment {
  position: absolute;
  top: 0;
  height: 100%;
  border-radius: 2px;
}

.pb-tl-bar .tl-segment.normal {
  background: rgba(200,191,255,.25);
}

.pb-tl-bar .tl-segment.motion {
  background: rgba(255,158,67,.5);
}

.pb-tl-bar .tl-segment.alert {
  background: rgba(255,107,107,.5);
}

.pb-tl-bar .tl-cursor {
  position: absolute;
  top: -2px;
  width: 2px;
  height: 20px;
  background: var(--pri);
  z-index: 2;
  cursor: ew-resize;
}

.pb-tl-bar .tl-cursor::after {
  content: '';
  position: absolute;
  top: -3px;
  left: -4px;
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: var(--pri);
  box-shadow: 0 0 6px rgba(200,191,255,.4);
}

.pb-tl-start, .pb-tl-end {
  position: absolute;
  top: 13px;
  font: 400 11px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.pb-tl-start {
  left: 0;
}

.pb-tl-end {
  right: 0;
}

@media (max-width: 1023px) {
  .pb-layout {
    grid-template-columns: 1fr;
    height: auto;
  }

  .pb-sidebar {
    max-height: 460px;
  }
}
</style>
