<script lang="ts">
  import type { DisplayInfo } from "./types";
  import { t } from "./i18n";

  let {
    display,
    customInputNames,
    onchange,
    onInputNameChange,
  }: {
    display: DisplayInfo;
    customInputNames?: Record<string, string>;
    onchange?: (updates: Partial<DisplayInfo>) => void;
    onInputNameChange?: (vcpCode: number, newName: string | null) => void;
  } = $props();

  let brightnessOverride = $state<number | null>(null);
  let brightness = $derived(brightnessOverride ?? display.brightness ?? 50);
  let statusMsg = $state("");
  let editingInputCode = $state<number | null>(null);
  let editingInputValue = $state("");

  // Auto-clear brightnessOverride after user stops dragging (sync with backend)
  let overrideClearTimer: ReturnType<typeof setTimeout> | null = null;

  function diagonalInches(): string {
    if (!display.screen_width_mm || !display.screen_height_mm) return "";
    const w = display.screen_width_mm / 25.4;
    const h = display.screen_height_mm / 25.4;
    return (Math.sqrt(w * w + h * h)).toFixed(1) + '"';
  }

  // ── Throttled brightness ──────────────────────────────────────────────
  let brightnessTimer: ReturnType<typeof setTimeout> | null = null;

  function onBrightnessInput(val: number) {
    brightnessOverride = val;
    if (brightnessTimer) clearTimeout(brightnessTimer);
    brightnessTimer = setTimeout(() => commitBrightness(val), 150);

    // Reset override after 3s of inactivity so periodic sync can update
    if (overrideClearTimer) clearTimeout(overrideClearTimer);
    overrideClearTimer = setTimeout(() => {
      brightnessOverride = null;
    }, 3000);
  }

  async function commitBrightness(val: number) {
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      await invoke("set_brightness", {
        displayIndex: display.display_index,
        level: val,
      });
      statusMsg = "";
    } catch (e: any) {
      statusMsg = `⚠ Brightness: ${e?.message ?? e}`;
      console.error("set_brightness:", e);
    }
  }

  async function switchInput(code: number) {
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const confirmedInput = await invoke<number>("set_input", {
        displayIndex: display.display_index,
        inputCode: code,
      });
      statusMsg = "";
      // Update local state with confirmed input source
      if (confirmedInput >= 0) {
        onchange?.({ current_input: confirmedInput });
      }
    } catch (e: any) {
      statusMsg = `⚠ Input: ${e?.message ?? e}`;
      console.error("set_input:", e);
    }
  }

  const DEFAULT_INPUT_NAMES: Record<number, string> = {
    1:  "VGA-1",
    2:  "VGA-2",
    3:  "DVI-1",
    4:  "DVI-2",
    5:  "Composite-1",
    6:  "Composite-2",
    7:  "S-Video-1",
    8:  "S-Video-2",
    9:  "Tuner-1",
    10: "Tuner-2",
    11: "Tuner-3",
    12: "Component-1",
    13: "Component-2",
    14: "Component-3",
    15: "DP-1",
    16: "DP-2",
    17: "HDMI-1",
    18: "HDMI-2",
    27: "USB-C",
  };

  function getInputName(code: number): string {
    const key = String(code);
    if (customInputNames?.[key]) return customInputNames[key];
    return DEFAULT_INPUT_NAMES[code] ?? `Input 0x${code.toString(16).toUpperCase()}`;
  }

  function startEditing(code: number) {
    editingInputCode = code;
    editingInputValue = getInputName(code);
  }

  function commitEdit(code: number) {
    const trimmed = editingInputValue.trim();
    const defaultName = DEFAULT_INPUT_NAMES[code] ?? `Input 0x${code.toString(16).toUpperCase()}`;
    // If the name matches default or is empty, reset to default (remove custom)
    if (!trimmed || trimmed === defaultName) {
      onInputNameChange?.(code, null);
    } else {
      onInputNameChange?.(code, trimmed);
    }
    editingInputCode = null;
  }

  function resetInputName(code: number) {
    onInputNameChange?.(code, null);
    editingInputCode = null;
  }

  function resetAllInputNames() {
    if (!display.supported_inputs) return;
    for (const code of display.supported_inputs) {
      onInputNameChange?.(code, null);
    }
    editingInputCode = null;
  }
</script>

<article class="card">
  <!-- Header -->
  <div class="header">
    <div class="icon">{display.is_internal ? "💻" : "🖥"}</div>
    <div class="info">
      <h2>{display.name}</h2>
      <span class="gpu">{display.gpu?.formatted_name ?? display.gpu?.name ?? "Unknown GPU"}</span>
      {#if display.manufacturer_id && display.product_code}
        <span class="model">{display.manufacturer_id} {display.product_code}</span>
      {/if}
    </div>
  </div>

  <!-- Info Grid -->
  <div class="grid">
    <div class="row">
      <span class="label">{t("card.interface")}</span>
      <span class="value accent">
        {display.hdmi_version ?? display.connection_type}
        {#if display.hdmi_frl_rate} {display.hdmi_frl_rate}{/if}
        {#if display.refresh_rate > 0} | {display.refresh_rate.toFixed(0)} Hz{/if}
      </span>
    </div>

    {#if display.resolution_str}
      <div class="row">
        <span class="label">{t("card.resolution")}</span>
        <span class="value accent">
          {display.resolution_str}
          {#if display.bits_per_pixel} ({display.bits_per_pixel}-bit){/if}
        </span>
      </div>
    {/if}

    {#if display.bandwidth?.bandwidth_str}
      <div class="row">
        <span class="label">{t("card.bandwidth")}</span>
        <span class="value accent">
          {display.bandwidth.bandwidth_str}
          {#if display.bandwidth.can_support_4k60} ✓ 4K@60{/if}
          {#if display.bandwidth.can_support_4k120} ✓ 4K@120{/if}
          {#if display.bandwidth.can_support_8k60} ✓ 8K@60{/if}
        </span>
      </div>
    {/if}

    {#if display.screen_width_mm > 0}
      <div class="row">
        <span class="label">{t("card.panel")}</span>
        <span class="value">{diagonalInches()} ({display.screen_width_mm / 10} × {display.screen_height_mm / 10} cm)</span>
      </div>
    {/if}

    {#if display.supports_hdr && display.hdr_formats?.length}
      <div class="row">
        <span class="label">{t("card.hdr")}</span>
        <span class="value hdr">{display.hdr_formats.join(", ")}</span>
      </div>
    {/if}
  </div>

  <!-- Brightness -->
  {#if display.brightness >= 0}
    <div class="brightness">
      <span class="label">{t("card.brightness")}</span>
      <input
        type="range"
        min="0"
        max="100"
        value={brightness}
        oninput={(e) => {
          const target = e.currentTarget;
          onBrightnessInput(Number(target.value));
        }}
      />
      <span class="val">{brightness}%</span>
    </div>
  {:else}
    <div class="brightness">
      <span class="label">{t("card.brightness")}</span>
      <span class="value" style="color:#666">{t("card.brightness.na")}</span>
    </div>
  {/if}

  <!-- Input Sources -->
  {#if !display.is_internal && display.supported_inputs?.length}
    <div class="inputs">
      <span class="label">{t("card.input")}</span>
      <div class="btn-group">
        {#each display.supported_inputs as code}
          {#if editingInputCode === code}
            <span class="input-edit-wrap">
              <!-- svelte-ignore a11y_autofocus -->
              <input
                type="text"
                class="input-name-edit"
                bind:value={editingInputValue}
                onkeydown={(e) => { if (e.key === 'Enter') commitEdit(code); if (e.key === 'Escape') editingInputCode = null; }}
                autofocus
              />
              <button class="edit-action-btn" onclick={() => commitEdit(code)} title={t("input.save")}>✓</button>
              <button class="edit-action-btn" onclick={() => resetInputName(code)} title={t("input.reset")}>↺</button>
            </span>
          {:else}
            <button
              class:active={code === display.current_input}
              onclick={() => switchInput(code)}
              ondblclick={(e) => { e.preventDefault(); startEditing(code); }}
              title={t("input.dblclick_edit")}
            >
              {getInputName(code)}
              {#if code === display.current_input} ✓{/if}
            </button>
          {/if}
        {/each}
      </div>
      {#if customInputNames && Object.keys(customInputNames).length > 0}
        <button class="reset-all-btn" onclick={resetAllInputNames} title={t("input.reset_all")}>↺</button>
      {/if}
    </div>
  {/if}

  <!-- Status message -->
  {#if statusMsg}
    <div class="status-msg">{statusMsg}</div>
  {/if}
</article>

<style>
  .card {
    background: #1a1b26;
    border-bottom: 1px solid #232433;
    padding: 14px 16px;
    transition: background 0.15s;
  }
  .card:hover { background: #1e1f2e; }

  .header {
    display: flex;
    gap: 10px;
    align-items: center;
    margin-bottom: 10px;
  }
  .icon {
    font-size: 1.3rem;
    width: 28px;
    text-align: center;
    flex-shrink: 0;
  }
  .info { display: flex; flex-direction: column; gap: 1px; min-width: 0; }
  h2 {
    margin: 0;
    font-size: 13px;
    font-weight: 600;
    color: #c0caf5;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .gpu { font-size: 11px; color: #565a89; }
  .model { font-size: 11px; color: #3b3f5c; }

  .grid {
    display: flex;
    flex-direction: column;
    gap: 3px;
    margin-bottom: 10px;
    padding-left: 38px;
  }
  .row { display: flex; gap: 8px; align-items: baseline; }
  .label {
    font-size: 11px;
    color: #565a89;
    min-width: 72px;
    text-align: right;
    flex-shrink: 0;
  }
  .value { font-size: 12px; color: #a9b1d6; }
  .accent { color: #7aa2f7; }
  .hdr { color: #e0af68; }

  .brightness {
    display: flex;
    align-items: center;
    gap: 8px;
    margin: 8px 0;
    padding-left: 38px;
  }
  .brightness input[type="range"] {
    flex: 1;
    accent-color: #7aa2f7;
    height: 4px;
  }
  .val { font-size: 12px; min-width: 36px; color: #a9b1d6; }

  .inputs {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-top: 6px;
    padding-left: 38px;
  }
  .btn-group { display: flex; gap: 4px; flex-wrap: wrap; }
  .btn-group button {
    padding: 3px 10px;
    border: 1px solid #24283b;
    border-radius: 3px;
    background: #24283b;
    color: #a9b1d6;
    cursor: pointer;
    font-size: 11px;
    transition: all 0.15s;
    font-family: inherit;
  }
  .btn-group button:hover {
    background: #2a2e47;
    border-color: #7aa2f7;
  }
  .btn-group button.active {
    background: #34548a;
    border-color: #7aa2f7;
    color: #c0caf5;
  }

  .status-msg {
    font-size: 11px;
    color: #f7768e;
    margin-top: 6px;
    padding: 4px 8px;
    padding-left: 38px;
    background: rgba(247, 118, 142, 0.08);
    border-left: 2px solid #f7768e;
  }

  .input-edit-wrap {
    display: inline-flex;
    align-items: center;
    gap: 2px;
  }
  .input-name-edit {
    width: 72px;
    padding: 2px 6px;
    font-size: 11px;
    font-family: inherit;
    border: 1px solid #7aa2f7;
    border-radius: 3px;
    background: #1a1b26;
    color: #c0caf5;
    outline: none;
  }
  .edit-action-btn {
    padding: 2px 5px;
    font-size: 11px;
    background: transparent;
    border: 1px solid #24283b;
    border-radius: 3px;
    color: #7aa2f7;
    cursor: pointer;
    font-family: inherit;
    line-height: 1;
  }
  .edit-action-btn:hover { background: #24283b; }
  .reset-all-btn {
    padding: 2px 5px;
    font-size: 12px;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 3px;
    color: #565a89;
    cursor: pointer;
    font-family: inherit;
    line-height: 1;
    flex-shrink: 0;
  }
  .reset-all-btn:hover { color: #f7768e; border-color: #f7768e; }

  /* ── Light theme ── */
  :global(html.light) .card {
    background: #d5d6db;
    border-bottom-color: #c4c5ca;
  }
  :global(html.light) .card:hover { background: #cdced3; }
  :global(html.light) h2 { color: #343b58; }
  :global(html.light) .gpu { color: #8990b3; }
  :global(html.light) .model { color: #9ca0b9; }
  :global(html.light) .label { color: #8990b3; }
  :global(html.light) .value { color: #343b58; }
  :global(html.light) .accent { color: #34548a; }
  :global(html.light) .hdr { color: #8f5e15; }
  :global(html.light) .val { color: #343b58; }
  :global(html.light) .brightness input[type="range"] { accent-color: #34548a; }
  :global(html.light) .btn-group button {
    background: #c4c5ca;
    border-color: #b8b9be;
    color: #343b58;
  }
  :global(html.light) .btn-group button:hover {
    background: #b8b9be;
    border-color: #34548a;
  }
  :global(html.light) .btn-group button.active {
    background: #34548a;
    border-color: #2e4a7a;
    color: #d5d6db;
  }
  :global(html.light) .status-msg {
    color: #8c4351;
    background: rgba(140, 67, 81, 0.08);
    border-left-color: #8c4351;
  }
  :global(html.light) .input-name-edit {
    background: #e8e9ee;
    color: #343b58;
    border-color: #34548a;
  }
  :global(html.light) .edit-action-btn { color: #34548a; }
  :global(html.light) .edit-action-btn:hover { background: #c4c5ca; }
  :global(html.light) .reset-all-btn { color: #8990b3; }
  :global(html.light) .reset-all-btn:hover { color: #8c4351; border-color: #8c4351; }
</style>
