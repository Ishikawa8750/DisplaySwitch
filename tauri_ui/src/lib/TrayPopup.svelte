<script lang="ts">
  import type { DisplayInfo, AppConfig } from "./types";
  import { loadConfig, saveConfig } from "./configStore";
  import { t } from "./i18n";

  let displays: DisplayInfo[] = $state([]);
  let loading = $state(true);
  let error = $state("");
  let appConfig = $state<AppConfig | null>(null);

  const DEFAULT_INPUT_NAMES: Record<number, string> = {
    1: "VGA-1", 2: "VGA-2", 3: "DVI-1", 4: "DVI-2",
    5: "Composite-1", 6: "Composite-2",
    15: "DP-1", 16: "DP-2", 17: "HDMI-1", 18: "HDMI-2", 27: "USB-C",
  };

  // Context menu for input rename
  let contextMenu = $state<{ x: number; y: number; code: number; displayIdx: number } | null>(null);
  let editingInput = $state<{ code: number; displayIdx: number } | null>(null);
  let editingValue = $state("");

  // Display rename
  let editingDisplayIdx = $state<number | null>(null);
  let editingDisplayValue = $state("");

  function displayId(d: DisplayInfo): string {
    return (d.manufacturer_id || "") + "_" + (d.product_code || "");
  }

  function getCustomNames(d: DisplayInfo): Record<string, string> {
    if (!appConfig) return {};
    return appConfig.custom_input_names[displayId(d)] ?? {};
  }

  function getInputName(d: DisplayInfo, code: number): string {
    const custom = getCustomNames(d);
    if (custom[String(code)]) return custom[String(code)];
    return DEFAULT_INPUT_NAMES[code] ?? `Input ${code}`;
  }

  function hasCustomName(d: DisplayInfo, code: number): boolean {
    return !!getCustomNames(d)[String(code)];
  }

  function diagonalInches(d: DisplayInfo): string {
    if (!d.screen_width_mm || !d.screen_height_mm) return "";
    const w = d.screen_width_mm / 25.4;
    const h = d.screen_height_mm / 25.4;
    return (Math.sqrt(w * w + h * h)).toFixed(0) + '"';
  }

  function getCustomDisplayName(d: DisplayInfo): string | null {
    if (!appConfig) return null;
    return appConfig.custom_display_names?.[displayId(d)] ?? null;
  }

  function displayLabel(d: DisplayInfo): string {
    // Check custom display name first
    const custom = getCustomDisplayName(d);
    if (custom) {
      const size = diagonalInches(d);
      return size ? `${size} ${custom}` : custom;
    }
    if (d.is_internal) return d.name || "Built-in Display";
    const size = diagonalInches(d);
    const name = d.name && d.name !== "Unknown Display" ? d.name : "";
    if (size && name) return `${size} ${name}`;
    if (name) return name;
    if (size) return `${size} External Display`;
    return d.name || "External Display";
  }

  function isTauri(): boolean {
    return !!(window as any).__TAURI_INTERNALS__;
  }

  async function refresh() {
    if (!isTauri()) return;
    loading = true;
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      displays = await invoke<DisplayInfo[]>("scan_monitors");
      appConfig = await loadConfig();
    } catch (e: any) {
      error = e?.message ?? String(e);
    }
    loading = false;
  }

  // Brightness throttle
  let brightnessTimers: Record<number, ReturnType<typeof setTimeout>> = {};
  let brightnessOverrides: Record<number, number> = $state({});

  function onBrightnessInput(idx: number, val: number) {
    brightnessOverrides = { ...brightnessOverrides, [idx]: val };
    if (brightnessTimers[idx]) clearTimeout(brightnessTimers[idx]);
    brightnessTimers[idx] = setTimeout(() => commitBrightness(idx, val), 80);
  }

  async function commitBrightness(idx: number, val: number) {
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      await invoke("set_brightness", { displayIndex: idx, level: val });
      if (displays[idx]) {
        displays[idx] = { ...displays[idx], brightness: val };
      }
    } catch {}
  }

  async function switchInput(idx: number, code: number) {
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const confirmed = await invoke<number>("set_input", {
        displayIndex: idx,
        inputCode: code,
      });
      if (confirmed >= 0 && displays[idx]) {
        displays[idx] = { ...displays[idx], current_input: confirmed };
      }
    } catch {}
  }

  function getBrightness(idx: number): number {
    return brightnessOverrides[idx] ?? displays[idx]?.brightness ?? 50;
  }

  // Context menu
  function openContextMenu(e: MouseEvent, code: number, displayIdx: number) {
    e.preventDefault();
    e.stopPropagation();
    contextMenu = { x: e.clientX, y: e.clientY, code, displayIdx };
  }

  function closeContextMenu() {
    contextMenu = null;
  }

  function startEditing(code: number, displayIdx: number) {
    editingInput = { code, displayIdx };
    editingValue = getInputName(displays[displayIdx], code);
    contextMenu = null;
  }

  async function commitEdit(code: number, displayIdx: number) {
    if (!appConfig) return;
    const d = displays[displayIdx];
    const id = displayId(d);
    if (!id || id === "_") return;

    const trimmed = editingValue.trim();
    const defaultName = DEFAULT_INPUT_NAMES[code] ?? `Input ${code}`;
    const names = { ...(appConfig.custom_input_names[id] ?? {}) };

    if (!trimmed || trimmed === defaultName) {
      delete names[String(code)];
    } else {
      names[String(code)] = trimmed;
    }

    const custom_input_names = { ...appConfig.custom_input_names };
    if (Object.keys(names).length === 0) {
      delete custom_input_names[id];
    } else {
      custom_input_names[id] = names;
    }

    appConfig = { ...appConfig, custom_input_names };
    await saveConfig(appConfig);
    editingInput = null;
  }

  async function resetInputName(code: number, displayIdx: number) {
    if (!appConfig) return;
    const d = displays[displayIdx];
    const id = displayId(d);
    if (!id || id === "_") return;

    const names = { ...(appConfig.custom_input_names[id] ?? {}) };
    delete names[String(code)];

    const custom_input_names = { ...appConfig.custom_input_names };
    if (Object.keys(names).length === 0) {
      delete custom_input_names[id];
    } else {
      custom_input_names[id] = names;
    }

    appConfig = { ...appConfig, custom_input_names };
    await saveConfig(appConfig);
    contextMenu = null;
  }

  // Display rename
  function startDisplayRename(idx: number) {
    const d = displays[idx];
    editingDisplayIdx = idx;
    editingDisplayValue = getCustomDisplayName(d) || d.name || "";
  }

  async function commitDisplayRename(idx: number) {
    if (!appConfig) return;
    const d = displays[idx];
    const id = displayId(d);
    if (!id || id === "_") return;

    const trimmed = editingDisplayValue.trim();
    const custom_display_names = { ...(appConfig.custom_display_names ?? {}) };

    if (!trimmed || trimmed === d.name) {
      delete custom_display_names[id];
    } else {
      custom_display_names[id] = trimmed;
    }

    appConfig = { ...appConfig, custom_display_names };
    await saveConfig(appConfig);
    editingDisplayIdx = null;
  }

  // Show main window
  async function showMainWindow() {
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      await invoke("show_main_window");
      // Hide popup
      const { getCurrentWindow } = await import("@tauri-apps/api/window");
      await getCurrentWindow().hide();
    } catch {}
  }

  // Quit app
  async function quitApp() {
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      await invoke("exit_app");
    } catch {}
  }

  // Auto-hide popup on blur
  async function handleBlur() {
    try {
      const { getCurrentWindow } = await import("@tauri-apps/api/window");
      const win = getCurrentWindow();
      await win.hide();
    } catch {}
  }

  function handleWindowClick() {
    if (contextMenu) contextMenu = null;
  }

  $effect(() => {
    refresh();
    const setupListener = async () => {
      try {
        const { getCurrentWindow } = await import("@tauri-apps/api/window");
        const win = getCurrentWindow();
        await win.onFocusChanged(({ payload: focused }) => {
          if (focused) refresh();
          else handleBlur();
        });
      } catch {}
    };
    setupListener();
  });
</script>

<svelte:window on:blur={handleBlur} onclick={handleWindowClick} />

<div class="popup">
  {#if loading && displays.length === 0}
    <div class="loading">
      <div class="spinner"></div>
    </div>
  {:else if error}
    <div class="error">{error}</div>
  {:else}
    <div class="displays">
      {#each displays as d, idx}
        <div class="display-card" class:internal={d.is_internal}>
          <!-- Header -->
          <div class="card-header">
            <div class="display-icon">
              {#if d.is_internal}
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                  <path d="M4 5a2 2 0 0 1 2-2h12a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V5z"/>
                  <path d="M2 20h20"/>
                  <path d="M9 17v3"/>
                  <path d="M15 17v3"/>
                </svg>
              {:else}
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                  <rect x="2" y="3" width="20" height="14" rx="2"/>
                  <line x1="8" y1="21" x2="16" y2="21"/>
                  <line x1="12" y1="17" x2="12" y2="21"/>
                </svg>
              {/if}
            </div>
            <div class="header-text">
              {#if editingDisplayIdx === idx}
                <span class="display-name-edit">
                  <!-- svelte-ignore a11y_autofocus -->
                  <input
                    type="text"
                    class="name-edit-field"
                    bind:value={editingDisplayValue}
                    onkeydown={(e) => { if (e.key === 'Enter') commitDisplayRename(idx); if (e.key === 'Escape') editingDisplayIdx = null; }}
                    autofocus
                  />
                  <button class="name-edit-confirm" onclick={() => commitDisplayRename(idx)}>✓</button>
                </span>
              {:else}
                <!-- svelte-ignore a11y_no_static_element_interactions -->
                <span class="display-name" ondblclick={() => startDisplayRename(idx)}>{displayLabel(d)}</span>
              {/if}
              <span class="display-meta">
                {d.resolution_str}{d.refresh_rate > 0 ? ` · ${d.refresh_rate.toFixed(0)}Hz` : ""}
                {#if !d.is_internal && d.connection_type && d.connection_type !== "External"}
                   · {d.connection_type}
                {/if}
              </span>
            </div>
          </div>

          <!-- Brightness -->
          {#if d.brightness >= 0}
            <div class="brightness-section">
              <div class="slider-row">
                <svg class="slider-icon" width="12" height="12" viewBox="0 0 24 24" fill="currentColor">
                  <circle cx="12" cy="12" r="5"/>
                  <path d="M12 1v2M12 21v2M4.22 4.22l1.42 1.42M18.36 18.36l1.42 1.42M1 12h2M21 12h2M4.22 19.78l1.42-1.42M18.36 5.64l1.42-1.42"/>
                </svg>
                <div class="slider-track">
                  <input
                    type="range"
                    min="0"
                    max="100"
                    value={getBrightness(idx)}
                    oninput={(e) => onBrightnessInput(idx, Number(e.currentTarget.value))}
                  />
                  <div class="slider-fill" style="width: {getBrightness(idx)}%"></div>
                </div>
                <span class="slider-value">{getBrightness(idx)}%</span>
              </div>
            </div>
          {/if}

          <!-- Input Sources -->
          {#if !d.is_internal && d.supported_inputs?.length}
            <div class="input-section">
              <div class="input-grid">
                {#each d.supported_inputs as code}
                  {#if editingInput?.code === code && editingInput?.displayIdx === idx}
                    <div class="input-edit">
                      <!-- svelte-ignore a11y_autofocus -->
                      <input
                        type="text"
                        class="edit-field"
                        bind:value={editingValue}
                        onkeydown={(e) => { if (e.key === 'Enter') commitEdit(code, idx); if (e.key === 'Escape') editingInput = null; }}
                        autofocus
                      />
                      <button class="edit-confirm" onclick={() => commitEdit(code, idx)}>✓</button>
                    </div>
                  {:else}
                    <button
                      class="input-chip"
                      class:active={code === d.current_input}
                      class:custom={hasCustomName(d, code)}
                      onclick={() => switchInput(idx, code)}
                      oncontextmenu={(e) => openContextMenu(e, code, idx)}
                      title="Click to switch · Right-click to rename"
                    >
                      <span class="chip-label">{getInputName(d, code)}</span>
                      {#if code === d.current_input}
                        <svg class="chip-check" width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round">
                          <polyline points="20 6 9 17 4 12"/>
                        </svg>
                      {/if}
                    </button>
                  {/if}
                {/each}
              </div>
            </div>
          {/if}
        </div>
      {/each}
    </div>

    <!-- Action Bar -->
    <div class="action-bar">
      <button class="action-btn" onclick={showMainWindow}>
        <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <rect x="2" y="3" width="20" height="14" rx="2"/>
          <line x1="8" y1="21" x2="16" y2="21"/>
          <line x1="12" y1="17" x2="12" y2="21"/>
        </svg>
        <span>Show Window</span>
      </button>
      <div class="action-sep"></div>
      <button class="action-btn quit" onclick={quitApp}>
        <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M18.36 6.64A9 9 0 1 1 5.64 6.64"/>
          <line x1="12" y1="2" x2="12" y2="12"/>
        </svg>
        <span>Quit</span>
      </button>
    </div>
  {/if}

  <!-- Context Menu -->
  {#if contextMenu}
    <div class="ctx-menu" style="left:{contextMenu.x}px;top:{contextMenu.y}px"
      onclick={(e) => e.stopPropagation()}>
      <button class="ctx-item" onclick={() => startEditing(contextMenu!.code, contextMenu!.displayIdx)}>
        ✏️ Rename
      </button>
      {#if hasCustomName(displays[contextMenu.displayIdx], contextMenu.code)}
        <button class="ctx-item" onclick={() => resetInputName(contextMenu!.code, contextMenu!.displayIdx)}>
          ↺ Reset Name
        </button>
      {/if}
    </div>
  {/if}
</div>

<style>
  :global(body) {
    margin: 0;
    padding: 0;
    background: transparent;
    overflow: hidden;
    font-family: -apple-system, BlinkMacSystemFont, "SF Pro Text", "SF Pro Display", "Helvetica Neue", system-ui, sans-serif;
    -webkit-font-smoothing: antialiased;
  }

  .popup {
    background: rgba(30, 30, 30, 0.78);
    border: 0.5px solid rgba(255, 255, 255, 0.12);
    border-radius: 14px;
    padding: 14px;
    color: #fff;
    box-shadow:
      0 0 0 0.5px rgba(0, 0, 0, 0.3),
      0 10px 40px rgba(0, 0, 0, 0.55),
      0 2px 12px rgba(0, 0, 0, 0.25);
    backdrop-filter: blur(60px) saturate(180%);
    -webkit-backdrop-filter: blur(60px) saturate(180%);
    min-height: 60px;
  }

  .loading {
    display: flex;
    justify-content: center;
    padding: 24px;
  }

  .spinner {
    width: 20px;
    height: 20px;
    border: 2px solid rgba(255, 255, 255, 0.15);
    border-top-color: rgba(255, 255, 255, 0.8);
    border-radius: 50%;
    animation: spin 0.6s linear infinite;
  }

  @keyframes spin {
    to { transform: rotate(360deg); }
  }

  .error {
    text-align: center;
    padding: 16px;
    font-size: 12px;
    color: #ff6b6b;
  }

  .displays {
    display: flex;
    flex-direction: column;
    gap: 10px;
  }

  .display-card {
    background: rgba(255, 255, 255, 0.06);
    border-radius: 10px;
    padding: 12px;
    transition: background 0.15s;
  }

  .display-card:hover {
    background: rgba(255, 255, 255, 0.09);
  }

  .card-header {
    display: flex;
    align-items: center;
    gap: 10px;
    margin-bottom: 8px;
  }

  .display-icon {
    color: rgba(255, 255, 255, 0.7);
    flex-shrink: 0;
    width: 20px;
    display: flex;
    align-items: center;
    justify-content: center;
  }

  .header-text {
    display: flex;
    flex-direction: column;
    gap: 1px;
    min-width: 0;
  }

  .display-name {
    font-size: 13px;
    font-weight: 600;
    color: #fff;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    letter-spacing: -0.01em;
  }

  .display-meta {
    font-size: 11px;
    color: rgba(255, 255, 255, 0.45);
    letter-spacing: -0.01em;
  }

  /* Brightness */
  .brightness-section {
    margin-bottom: 6px;
  }

  .slider-row {
    display: flex;
    align-items: center;
    gap: 8px;
    height: 28px;
    background: rgba(255, 255, 255, 0.06);
    border-radius: 8px;
    padding: 0 10px;
  }

  .slider-icon {
    color: rgba(255, 255, 255, 0.55);
    flex-shrink: 0;
  }

  .slider-track {
    flex: 1;
    position: relative;
    height: 4px;
  }

  .slider-track input[type="range"] {
    position: absolute;
    top: 50%;
    left: 0;
    width: 100%;
    transform: translateY(-50%);
    -webkit-appearance: none;
    appearance: none;
    background: transparent;
    cursor: pointer;
    z-index: 2;
    margin: 0;
    height: 16px;
  }

  .slider-track input[type="range"]::-webkit-slider-runnable-track {
    height: 4px;
    border-radius: 2px;
    background: rgba(255, 255, 255, 0.12);
  }

  .slider-track input[type="range"]::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 14px;
    height: 14px;
    border-radius: 50%;
    background: #fff;
    margin-top: -5px;
    box-shadow: 0 1px 4px rgba(0, 0, 0, 0.4);
    transition: transform 0.1s;
  }

  .slider-track input[type="range"]::-webkit-slider-thumb:active {
    transform: scale(1.15);
  }

  .slider-fill {
    position: absolute;
    top: 50%;
    left: 0;
    height: 4px;
    transform: translateY(-50%);
    background: #fff;
    border-radius: 2px;
    pointer-events: none;
    z-index: 1;
  }

  .slider-value {
    font-size: 11px;
    color: rgba(255, 255, 255, 0.55);
    min-width: 32px;
    text-align: right;
    font-variant-numeric: tabular-nums;
    flex-shrink: 0;
  }

  /* Input Sources */
  .input-section {
    margin-top: 2px;
  }

  .input-grid {
    display: flex;
    flex-wrap: wrap;
    gap: 4px;
  }

  .input-chip {
    display: inline-flex;
    align-items: center;
    gap: 4px;
    font-size: 11px;
    padding: 5px 10px;
    border: none;
    border-radius: 6px;
    background: rgba(255, 255, 255, 0.08);
    color: rgba(255, 255, 255, 0.75);
    cursor: pointer;
    transition: all 0.15s;
    font-family: inherit;
    letter-spacing: -0.01em;
  }

  .input-chip:hover {
    background: rgba(255, 255, 255, 0.14);
    color: #fff;
  }

  .input-chip:active {
    background: rgba(255, 255, 255, 0.18);
    transform: scale(0.97);
  }

  .input-chip.active {
    background: rgba(10, 132, 255, 0.3);
    color: #64b5ff;
    font-weight: 500;
  }

  .input-chip.active:hover {
    background: rgba(10, 132, 255, 0.38);
  }

  .input-chip.custom {
    border: 1px dashed rgba(255, 255, 255, 0.2);
  }

  .chip-label {
    line-height: 1;
  }

  .chip-check {
    color: #64b5ff;
    flex-shrink: 0;
  }

  /* Edit */
  .input-edit {
    display: inline-flex;
    align-items: center;
    gap: 3px;
    animation: pop 0.15s ease-out;
  }

  @keyframes pop {
    from { opacity: 0; transform: scale(0.92); }
    to { opacity: 1; transform: scale(1); }
  }

  .edit-field {
    width: 80px;
    padding: 4px 8px;
    font-size: 11px;
    font-family: inherit;
    border: 1px solid rgba(10, 132, 255, 0.5);
    border-radius: 6px;
    background: rgba(0, 0, 0, 0.3);
    color: #fff;
    outline: none;
  }

  .edit-field:focus {
    border-color: rgba(10, 132, 255, 0.8);
    box-shadow: 0 0 0 2px rgba(10, 132, 255, 0.2);
  }

  .edit-confirm {
    padding: 4px 6px;
    font-size: 11px;
    background: rgba(10, 132, 255, 0.3);
    border: none;
    border-radius: 5px;
    color: #64b5ff;
    cursor: pointer;
    font-family: inherit;
    transition: background 0.12s;
  }

  .edit-confirm:hover {
    background: rgba(10, 132, 255, 0.45);
  }

  /* Context Menu */
  .ctx-menu {
    position: fixed;
    z-index: 9999;
    background: rgba(40, 40, 40, 0.95);
    border: 0.5px solid rgba(255, 255, 255, 0.15);
    border-radius: 8px;
    padding: 4px;
    min-width: 140px;
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.5);
    backdrop-filter: blur(40px);
    -webkit-backdrop-filter: blur(40px);
    animation: ctx-in 0.12s ease-out;
  }

  @keyframes ctx-in {
    from { opacity: 0; transform: scale(0.96) translateY(-2px); }
    to { opacity: 1; transform: scale(1) translateY(0); }
  }

  .ctx-item {
    display: block;
    width: 100%;
    padding: 6px 10px;
    border: none;
    border-radius: 5px;
    background: transparent;
    color: rgba(255, 255, 255, 0.85);
    font-size: 12px;
    font-family: inherit;
    text-align: left;
    cursor: pointer;
    transition: background 0.1s;
  }

  .ctx-item:hover {
    background: rgba(10, 132, 255, 0.25);
    color: #fff;
  }

  /* Display name edit */
  .display-name {
    cursor: default;
  }

  .display-name-edit {
    display: flex;
    align-items: center;
    gap: 4px;
  }

  .name-edit-field {
    width: 140px;
    padding: 2px 6px;
    font-size: 13px;
    font-weight: 600;
    font-family: inherit;
    border: 1px solid rgba(10, 132, 255, 0.5);
    border-radius: 5px;
    background: rgba(0, 0, 0, 0.3);
    color: #fff;
    outline: none;
  }

  .name-edit-field:focus {
    border-color: rgba(10, 132, 255, 0.8);
    box-shadow: 0 0 0 2px rgba(10, 132, 255, 0.15);
  }

  .name-edit-confirm {
    padding: 2px 6px;
    font-size: 12px;
    background: rgba(10, 132, 255, 0.3);
    border: none;
    border-radius: 4px;
    color: #64b5ff;
    cursor: pointer;
    font-family: inherit;
  }

  .name-edit-confirm:hover {
    background: rgba(10, 132, 255, 0.45);
  }

  /* Action Bar */
  .action-bar {
    display: flex;
    align-items: center;
    gap: 0;
    margin-top: 10px;
    background: rgba(255, 255, 255, 0.04);
    border-radius: 8px;
    overflow: hidden;
  }

  .action-btn {
    flex: 1;
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 6px;
    padding: 8px 0;
    border: none;
    background: transparent;
    color: rgba(255, 255, 255, 0.7);
    font-size: 12px;
    font-family: inherit;
    cursor: pointer;
    transition: all 0.12s;
  }

  .action-btn:hover {
    background: rgba(255, 255, 255, 0.08);
    color: #fff;
  }

  .action-btn:active {
    background: rgba(255, 255, 255, 0.12);
  }

  .action-btn.quit:hover {
    background: rgba(255, 80, 80, 0.12);
    color: #ff6b6b;
  }

  .action-sep {
    width: 1px;
    height: 20px;
    background: rgba(255, 255, 255, 0.08);
    flex-shrink: 0;
  }
</style>
