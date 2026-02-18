<script setup>
</script>

<template>
  <div class="monitor-page">
    <div class="monitor-toolbar">
      <div class="layout-btns">
        <button class="layout-btn">1x1</button>
        <button class="layout-btn active">2x2</button>
        <button class="layout-btn">3x3</button>
        <button class="layout-btn">4x4</button>
      </div>
      <div class="mt-actions">
        <button class="mt-btn">
          <span class="mi">add</span> Add Camera
        </button>
        <button class="mt-btn primary">
          <span class="mi">play_arrow</span> Start Patrol
        </button>
      </div>
    </div>
    <div class="video-grid g2x2">
      <div class="video-cell">
        <div class="sv-lines"></div>
        <div class="sv-grid"></div>
        <div class="sv-icon"><span class="mi">videocam</span></div>
        <div class="vc-top">
          <span class="cam-name">Front Door</span>
          <div class="live-badge"><span class="dot blink"></span> LIVE</div>
        </div>
        <div class="vc-bottom">
          <span class="ts">14:23:05</span>
          <div class="vc-actions">
            <button class="vc-btn"><span class="mi">photo_camera</span></button>
            <button class="vc-btn"><span class="mi">fiber_manual_record</span></button>
            <button class="vc-btn"><span class="mi">open_in_full</span></button>
          </div>
        </div>
      </div>
      <div class="video-cell">
        <div class="sv-lines"></div>
        <div class="sv-grid"></div>
        <div class="sv-icon"><span class="mi">videocam</span></div>
        <div class="vc-top">
          <span class="cam-name">Parking Lot</span>
          <div class="live-badge"><span class="dot blink"></span> LIVE</div>
        </div>
        <div class="vc-bottom">
          <span class="ts">14:23:05</span>
          <div class="vc-actions">
            <button class="vc-btn"><span class="mi">photo_camera</span></button>
            <button class="vc-btn"><span class="mi">fiber_manual_record</span></button>
            <button class="vc-btn"><span class="mi">open_in_full</span></button>
          </div>
        </div>
      </div>
      <div class="video-cell empty">
        <div class="empty-hint">
          <span class="mi">add</span>
          <span>Drag camera here</span>
        </div>
      </div>
      <div class="video-cell">
        <div class="offline-overlay">
          <span class="mi">videocam_off</span>
          <span>Offline</span>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.monitor-page {
  display: flex;
  flex-direction: column;
  height: calc(100vh - var(--topbar-h) - 48px);
}

.monitor-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 16px;
  gap: 12px;
  flex-wrap: wrap;
}

.layout-btns {
  display: flex;
  gap: 4px;
}

.layout-btn {
  width: 36px;
  height: 36px;
  border-radius: var(--r2);
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all .15s;
  font: 500 12px/16px 'Roboto', sans-serif;
}

.layout-btn:hover {
  background: var(--sc2);
}

.layout-btn.active {
  background: var(--pri-c);
  color: var(--on-pri-c);
  border-color: var(--pri-c);
}

.mt-actions {
  display: flex;
  gap: 8px;
  align-items: center;
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

.video-grid {
  display: grid;
  gap: 4px;
  flex: 1;
  min-height: 0;
}

.video-grid.g2x2 {
  grid-template-columns: 1fr 1fr;
  grid-template-rows: 1fr 1fr;
}

.video-cell {
  position: relative;
  background: linear-gradient(135deg, #0d0d15, #1c1728);
  border-radius: var(--r2);
  overflow: hidden;
  min-height: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: outline .15s;
}

.video-cell:hover {
  outline: 2px solid var(--pri);
  outline-offset: -2px;
}

.video-cell .sv-lines {
  position: absolute;
  inset: 0;
  background: repeating-linear-gradient(0deg, transparent, transparent 2px, rgba(255,255,255,.01) 2px, rgba(255,255,255,.01) 4px);
  pointer-events: none;
}

.video-cell .sv-grid {
  position: absolute;
  inset: 0;
  background: linear-gradient(rgba(255,255,255,.015) 1px, transparent 1px), linear-gradient(90deg, rgba(255,255,255,.015) 1px, transparent 1px);
  background-size: 20px 20px;
  pointer-events: none;
}

.video-cell .sv-icon {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
}

.video-cell .sv-icon .mi {
  font-size: 42px;
  color: rgba(255,255,255,.04);
}

.video-cell .vc-top {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  padding: 8px 10px;
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  background: linear-gradient(to bottom, rgba(0,0,0,.6), transparent);
  z-index: 3;
}

.video-cell .vc-top .cam-name {
  font: 500 12px/18px 'Roboto', sans-serif;
  color: #fff;
}

.video-cell .vc-top .live-badge {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 2px 7px;
  border-radius: var(--r1);
  background: rgba(200,60,60,.9);
  font: 500 9px/14px 'Roboto', sans-serif;
  color: #fff;
}

.video-cell .vc-top .live-badge .dot {
  width: 4px;
  height: 4px;
  border-radius: 50%;
  background: #fff;
}

.video-cell .vc-bottom {
  position: absolute;
  bottom: 0;
  left: 0;
  right: 0;
  padding: 8px 10px;
  background: linear-gradient(to top, rgba(0,0,0,.6), transparent);
  display: flex;
  justify-content: space-between;
  align-items: center;
  z-index: 3;
}

.video-cell .vc-bottom .ts {
  font: 400 11px/16px 'Roboto', sans-serif;
  color: rgba(255,255,255,.7);
}

.video-cell .vc-bottom .vc-actions {
  display: flex;
  gap: 6px;
}

.video-cell .vc-bottom .vc-btn {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  background: rgba(255,255,255,.1);
  border: none;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  backdrop-filter: blur(4px);
}

.video-cell .vc-bottom .vc-btn .mi {
  font-size: 16px;
  color: #fff;
}

.video-cell .vc-bottom .vc-btn:hover {
  background: rgba(255,255,255,.2);
}

.video-cell.empty {
  background: var(--sc);
  border: 2px dashed var(--olv);
}

.video-cell.empty .empty-hint {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--ol);
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
}

.video-cell.empty .empty-hint .mi {
  font-size: 32px;
  color: var(--olv);
}

.video-cell .offline-overlay {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  background: rgba(15,13,19,.8);
  z-index: 2;
}

.video-cell .offline-overlay .mi {
  font-size: 32px;
  color: var(--red);
  margin-bottom: 6px;
}

.video-cell .offline-overlay span {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--red);
}
</style>
