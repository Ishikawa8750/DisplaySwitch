<script lang="ts">
  import type { DisplayInfo } from "./types";
  import { t } from "./i18n";

  let displays: DisplayInfo[] = $state([]);
  let loading = $state(true);
  let error = $state("");

  const DEFAULT_INPUT_NAMES: Record<number, string> = {
    1: "VGA-1", 2: "VGA-2", 3: "DVI-1", 4: "DVI-2",
    15: "DP-1", 16: "DP-2", 17: "HDMI-1", 18: "HDMI-2", 27: "USB-C",
  };

  function getInputName(code: number): string {
    return DEFAULT_INPUT_NAMES[code] ?? `Input ${code}`;
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
    } catch (e: any) {
      error = e?.message ?? String(e);
    }
    loading = false;
  }

  // Throttled brightness
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

  // Auto-hide popup when it loses focus
  async function handleBlur() {
    try {
      const { getCurrentWindow } = await import("@tauri-apps/api/window");
      const win = getCurrentWindow();
      await win.hide();
    } catch {}
  }

  // Load on mount
  $effect(() => {
    refresh();
    // Listen for window focus to refresh data
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

<svelte:window on:blur={handleBlur} />

<div class="popup">
  {#if loading && displays.length === 0}
    <div class="loading">Loading...</div>
  {:else if error}
    <div class="error">{error}</div>
  {:else}
    <div class="displays">
      {#each displays as d, idx}
        <div class="display-col">
          <div class="display-header">
            <span class="display-icon">{d.is_internal ? "💻" : "🖥"}</span>
            <span class="display-name" title={d.name}>
              {d.name.length > 18 ? d.name.slice(0, 16) + "…" : d.name}
            </span>
          </div>

          <!-- Connection info -->
          <div class="conn-info">
            {d.connection_type}{d.refresh_rate > 0 ? ` ${d.refresh_rate.toFixed(0)}Hz` : ""}
          </div>

          <!-- Brightness -->
          {#if d.brightness >= 0}
            <div class="brightness-row">
              <span class="brightness-icon">☀</span>
              <input
                type="range"
                min="0"
                max="100"
                value={getBrightness(idx)}
                oninput={(e) => onBrightnessInput(idx, Number(e.currentTarget.value))}
              />
              <span class="brightness-val">{getBrightness(idx)}%</span>
            </div>
          {/if}

          <!-- Input Sources (external only) -->
          {#if !d.is_internal && d.supported_inputs?.length}
            <div class="input-section">
              <div class="input-label">{t("card.input")}</div>
              <div class="input-grid">
                {#each d.supported_inputs as code}
                  <button
                    class="input-btn"
                    class:active={code === d.current_input}
                    onclick={() => switchInput(idx, code)}
                    title={getInputName(code)}
                  >
                    {getInputName(code)}
                  </button>
                {/each}
              </div>
            </div>
          {/if}
        </div>

        {#if idx < displays.length - 1}
          <div class="divider"></div>
        {/if}
      {/each}
    </div>
  {/if}
</div>

<style>
  :global(body) {
    margin: 0;
    padding: 0;
    background: transparent;
    overflow: hidden;
  }
  .popup {
    background: rgba(26, 27, 38, 0.96);
    border: 1px solid #2a2b3d;
    border-radius: 12px;
    padding: 12px;
    font-family: -apple-system, BlinkMacSystemFont, "SF Pro Text", sans-serif;
    color: #c0caf5;
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.5);
    backdrop-filter: blur(20px);
    -webkit-backdrop-filter: blur(20px);
    min-height: 80px;
  }

  .loading, .error {
    text-align: center;
    padding: 20px;
    font-size: 12px;
    color: #565a89;
  }
  .error { color: #f7768e; }

  .displays {
    display: flex;
    gap: 0;
    align-items: stretch;
  }

  .display-col {
    flex: 1;
    min-width: 0;
    padding: 4px 10px;
  }

  .divider {
    width: 1px;
    background: #2a2b3d;
    align-self: stretch;
    flex-shrink: 0;
  }

  .display-header {
    display: flex;
    align-items: center;
    gap: 6px;
    margin-bottom: 4px;
  }

  .display-icon {
    font-size: 14px;
    flex-shrink: 0;
  }

  .display-name {
    font-size: 12px;
    font-weight: 600;
    color: #c0caf5;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .conn-info {
    font-size: 10px;
    color: #7aa2f7;
    margin-bottom: 6px;
    padding-left: 20px;
  }

  .brightness-row {
    display: flex;
    align-items: center;
    gap: 6px;
    margin-bottom: 6px;
  }

  .brightness-icon {
    font-size: 12px;
    color: #e0af68;
    flex-shrink: 0;
    width: 14px;
    text-align: center;
  }

  .brightness-row input[type="range"] {
    flex: 1;
    height: 4px;
    accent-color: #7aa2f7;
    cursor: pointer;
    -webkit-appearance: none;
    appearance: none;
    background: #2a2b3d;
    border-radius: 2px;
    outline: none;
  }

  .brightness-row input[type="range"]::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 12px;
    height: 12px;
    border-radius: 50%;
    background: #7aa2f7;
    cursor: pointer;
    border: none;
  }

  .brightness-val {
    font-size: 11px;
    color: #a9b1d6;
    min-width: 32px;
    text-align: right;
    font-variant-numeric: tabular-nums;
  }

  .input-section {
    margin-top: 2px;
  }

  .input-label {
    font-size: 10px;
    color: #565a89;
    margin-bottom: 4px;
  }

  .input-grid {
    display: flex;
    flex-wrap: wrap;
    gap: 3px;
  }

  .input-btn {
    font-size: 10px;
    padding: 3px 7px;
    border: 1px solid #2a2b3d;
    border-radius: 4px;
    background: #16161e;
    color: #a9b1d6;
    cursor: pointer;
    transition: all 0.15s;
    white-space: nowrap;
  }

  .input-btn:hover {
    background: #1e1f2e;
    border-color: #7aa2f7;
    color: #c0caf5;
  }

  .input-btn.active {
    background: #24283b;
    border-color: #7aa2f7;
    color: #7aa2f7;
    font-weight: 600;
  }
</style>
