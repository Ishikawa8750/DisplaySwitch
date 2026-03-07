<!--
  TitleBar — Windows-only custom title bar (VS Code style)
  Provides: drag region, app title, window controls (minimize / maximize / close).
  On macOS this component should NOT be rendered (native title bar used instead).
-->
<script lang="ts">
  import { t } from "./i18n";

  let { isMaximized = false }: { isMaximized?: boolean } = $props();

  async function minimize() {
    const { invoke } = await import("@tauri-apps/api/core");
    await invoke("window_minimize");
  }

  async function toggleMaximize() {
    const { invoke } = await import("@tauri-apps/api/core");
    await invoke("window_toggle_maximize");
  }

  async function close() {
    const { invoke } = await import("@tauri-apps/api/core");
    await invoke("window_close");
  }

  async function startDrag(e: MouseEvent) {
    // Only drag on left button, not on buttons
    if (e.button !== 0) return;
    const { getCurrentWindow } = await import("@tauri-apps/api/window");
    await getCurrentWindow().startDragging();
  }
</script>

<!-- svelte-ignore a11y_no_static_element_interactions -->
<div class="titlebar" onmousedown={startDrag} ondblclick={toggleMaximize}>
  <div class="titlebar-left">
    <span class="titlebar-icon">🖥</span>
    <span class="titlebar-title">DisplaySwitch</span>
  </div>

  <div class="titlebar-spacer"></div>

  <!-- Window controls -->
  <!-- svelte-ignore a11y_click_events_have_key_events -->
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div class="window-controls" onmousedown={(e) => e.stopPropagation()}>
    <button class="win-btn" onclick={minimize} title="Minimize">
      <svg width="10" height="1" viewBox="0 0 10 1">
        <rect width="10" height="1" fill="currentColor"/>
      </svg>
    </button>
    <button class="win-btn" onclick={toggleMaximize} title={isMaximized ? "Restore" : "Maximize"}>
      {#if isMaximized}
        <svg width="10" height="10" viewBox="0 0 10 10">
          <path d="M2 0h8v8H8v-1h1V1H3v1H2V0z" fill="currentColor"/>
          <rect x="0" y="2" width="8" height="8" fill="none" stroke="currentColor" stroke-width="1"/>
        </svg>
      {:else}
        <svg width="10" height="10" viewBox="0 0 10 10">
          <rect width="10" height="10" fill="none" stroke="currentColor" stroke-width="1"/>
        </svg>
      {/if}
    </button>
    <button class="win-btn win-close" onclick={close} title="Close">
      <svg width="10" height="10" viewBox="0 0 10 10">
        <path d="M1 0L5 4L9 0L10 1L6 5L10 9L9 10L5 6L1 10L0 9L4 5L0 1Z" fill="currentColor"/>
      </svg>
    </button>
  </div>
</div>

<style>
  .titlebar {
    height: 30px;
    min-height: 30px;
    display: flex;
    align-items: center;
    background: #16161e;
    border-bottom: 1px solid #101014;
    user-select: none;
    -webkit-app-region: drag;
    z-index: 1000;
    font-family: "Segoe UI", -apple-system, sans-serif;
  }

  :global(html.light) .titlebar {
    background: #c4c5ca;
    border-bottom-color: #b8b9be;
  }

  .titlebar-left {
    display: flex;
    align-items: center;
    gap: 6px;
    padding-left: 10px;
    pointer-events: none;
  }

  .titlebar-icon {
    font-size: 13px;
    line-height: 1;
  }

  .titlebar-title {
    font-size: 12px;
    color: #787c99;
    font-weight: 400;
  }

  :global(html.light) .titlebar-title {
    color: #6e7191;
  }

  .titlebar-spacer { flex: 1; }

  .window-controls {
    display: flex;
    -webkit-app-region: no-drag;
    height: 100%;
  }

  .win-btn {
    width: 46px;
    height: 100%;
    border: none;
    background: transparent;
    color: #787c99;
    cursor: pointer;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 0;
    transition: background 0.1s;
  }

  .win-btn:hover {
    background: rgba(255, 255, 255, 0.06);
    color: #c0caf5;
  }

  :global(html.light) .win-btn {
    color: #6e7191;
  }

  :global(html.light) .win-btn:hover {
    background: rgba(0, 0, 0, 0.06);
    color: #343b58;
  }

  .win-close:hover {
    background: #e81123 !important;
    color: #fff !important;
  }
</style>
