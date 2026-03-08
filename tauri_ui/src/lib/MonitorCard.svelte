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

  // Context menu for input rename
  let contextMenu = $state<{ x: number; y: number; code: number } | null>(null);

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

  function hasCustomName(code: number): boolean {
    return !!customInputNames?.[String(code)];
  }

  function openContextMenu(e: MouseEvent, code: number) {
    e.preventDefault();
    e.stopPropagation();
    contextMenu = { x: e.clientX, y: e.clientY, code };
  }

  function closeContextMenu() {
    contextMenu = null;
  }

  function startEditing(code: number) {
    editingInputCode = code;
    editingInputValue = getInputName(code);
    contextMenu = null;
  }

  function commitEdit(code: number) {
    const trimmed = editingInputValue.trim();
    const defaultName = DEFAULT_INPUT_NAMES[code] ?? `Input 0x${code.toString(16).toUpperCase()}`;
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
    contextMenu = null;
  }

  function resetAllInputNames() {
    if (!display.supported_inputs) return;
    for (const code of display.supported_inputs) {
      onInputNameChange?.(code, null);
    }
    editingInputCode = null;
    contextMenu = null;
  }

  // Close context menu on outside click
  function handleWindowClick() {
    if (contextMenu) contextMenu = null;
  }
</script>

<svelte:window onclick={handleWindowClick} />

<article class="card">
  <!-- Header -->
  <div class="header">
    <div class="icon-wrap">
      {#if display.is_internal}
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">
          <path d="M4 5a2 2 0 0 1 2-2h12a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V5z"/>
          <path d="M2 20h20"/>
          <path d="M9 17v3"/>
          <path d="M15 17v3"/>
        </svg>
      {:else}
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">
          <rect x="2" y="3" width="20" height="14" rx="2"/>
          <line x1="8" y1="21" x2="16" y2="21"/>
          <line x1="12" y1="17" x2="12" y2="21"/>
        </svg>
      {/if}
    </div>
    <div class="info">
      <h2>
        {#if !display.is_internal && diagonalInches()}
          <span class="size-badge">{diagonalInches()}</span>
        {/if}
        {display.name}
      </h2>
      <span class="subtitle">
        {display.gpu?.formatted_name ?? display.gpu?.name ?? ""}
        {#if display.manufacturer_id && display.product_code}
          <span class="sep">·</span>{display.manufacturer_id} {display.product_code}
        {/if}
      </span>
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
            </span>
          {:else}
            <button
              class:active={code === display.current_input}
              class:custom-named={hasCustomName(code)}
              onclick={() => switchInput(code)}
              oncontextmenu={(e) => openContextMenu(e, code)}
              title={t("input.right_click_edit")}
            >
              {getInputName(code)}
              {#if code === display.current_input} ✓{/if}
            </button>
          {/if}
        {/each}
      </div>
    </div>
  {/if}

  <!-- Context Menu -->
  {#if contextMenu}
    <div class="ctx-menu" style="left:{contextMenu.x}px;top:{contextMenu.y}px"
      onclick={(e) => e.stopPropagation()}>
      <button class="ctx-item" onclick={() => startEditing(contextMenu.code)}>
        ✏️ {t("input.rename")}
      </button>
      {#if hasCustomName(contextMenu.code)}
        <button class="ctx-item" onclick={() => resetInputName(contextMenu.code)}>
          ↺ {t("input.reset")}
        </button>
      {/if}
      {#if customInputNames && Object.keys(customInputNames).length > 0}
        <div class="ctx-sep"></div>
        <button class="ctx-item danger" onclick={resetAllInputNames}>
          ↺ {t("input.reset_all")}
        </button>
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
    background: rgba(255, 255, 255, 0.04);
    border: 1px solid rgba(255, 255, 255, 0.06);
    border-radius: 12px;
    padding: 16px;
    transition: background 0.2s, border-color 0.2s;
    animation: card-enter 0.3s ease-out both;
  }
  .card:hover {
    background: rgba(255, 255, 255, 0.06);
    border-color: rgba(255, 255, 255, 0.1);
  }

  @keyframes card-enter {
    from { opacity: 0; transform: translateY(8px); }
    to   { opacity: 1; transform: translateY(0); }
  }

  .header {
    display: flex;
    gap: 12px;
    align-items: center;
    margin-bottom: 14px;
  }
  .icon-wrap {
    width: 36px;
    height: 36px;
    display: flex;
    align-items: center;
    justify-content: center;
    border-radius: 10px;
    background: rgba(122, 162, 247, 0.1);
    color: #7aa2f7;
    flex-shrink: 0;
    transition: transform 0.2s;
  }
  .card:hover .icon-wrap { transform: scale(1.05); }

  .info { display: flex; flex-direction: column; gap: 2px; min-width: 0; }
  h2 {
    margin: 0;
    font-size: 14px;
    font-weight: 600;
    color: #e1e4f0;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    display: flex;
    align-items: center;
    gap: 6px;
    letter-spacing: -0.01em;
  }
  .size-badge {
    font-size: 11px;
    font-weight: 500;
    padding: 1px 6px;
    border-radius: 4px;
    background: rgba(122, 162, 247, 0.15);
    color: #7aa2f7;
    flex-shrink: 0;
  }
  .subtitle { font-size: 11px; color: rgba(169, 177, 214, 0.6); }
  .sep { margin: 0 2px; }

  .grid {
    display: flex;
    flex-direction: column;
    gap: 4px;
    margin-bottom: 12px;
    padding-left: 48px;
  }
  .row {
    display: flex;
    gap: 10px;
    align-items: baseline;
    animation: row-enter 0.25s ease-out both;
  }
  .row:nth-child(1) { animation-delay: 0.05s; }
  .row:nth-child(2) { animation-delay: 0.1s; }
  .row:nth-child(3) { animation-delay: 0.15s; }
  .row:nth-child(4) { animation-delay: 0.2s; }
  .row:nth-child(5) { animation-delay: 0.25s; }

  @keyframes row-enter {
    from { opacity: 0; transform: translateX(-6px); }
    to   { opacity: 1; transform: translateX(0); }
  }

  .label {
    font-size: 11px;
    color: rgba(169, 177, 214, 0.5);
    min-width: 72px;
    text-align: right;
    flex-shrink: 0;
    font-weight: 500;
  }
  .value { font-size: 12px; color: rgba(169, 177, 214, 0.85); }
  .accent { color: #7aa2f7; }
  .hdr { color: #e0af68; }

  .brightness {
    display: flex;
    align-items: center;
    gap: 10px;
    margin: 10px 0;
    padding: 8px 12px;
    padding-left: 48px;
    background: rgba(255, 255, 255, 0.03);
    border-radius: 8px;
  }
  .brightness input[type="range"] {
    flex: 1;
    -webkit-appearance: none;
    appearance: none;
    height: 4px;
    border-radius: 2px;
    background: rgba(255, 255, 255, 0.1);
    outline: none;
    cursor: pointer;
  }
  .brightness input[type="range"]::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 14px;
    height: 14px;
    border-radius: 50%;
    background: #7aa2f7;
    box-shadow: 0 1px 4px rgba(0, 0, 0, 0.3);
    transition: transform 0.1s;
  }
  .brightness input[type="range"]::-webkit-slider-thumb:active {
    transform: scale(1.15);
  }
  .val {
    font-size: 12px;
    min-width: 36px;
    color: rgba(169, 177, 214, 0.7);
    font-variant-numeric: tabular-nums;
  }

  .inputs {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-top: 8px;
    padding-left: 48px;
  }
  .btn-group { display: flex; gap: 6px; flex-wrap: wrap; }
  .btn-group button {
    padding: 5px 12px;
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 8px;
    background: rgba(255, 255, 255, 0.05);
    color: rgba(169, 177, 214, 0.8);
    cursor: pointer;
    font-size: 11px;
    font-weight: 500;
    transition: all 0.15s;
    font-family: inherit;
    letter-spacing: -0.01em;
  }
  .btn-group button:hover {
    background: rgba(255, 255, 255, 0.1);
    border-color: rgba(122, 162, 247, 0.3);
    color: #e1e4f0;
  }
  .btn-group button:active {
    transform: scale(0.97);
  }
  .btn-group button.active {
    background: rgba(122, 162, 247, 0.18);
    border-color: rgba(122, 162, 247, 0.35);
    color: #7aa2f7;
  }
  .btn-group button.custom-named {
    border-style: dashed;
  }

  .status-msg {
    font-size: 11px;
    color: #f7768e;
    margin-top: 6px;
    padding: 6px 10px;
    padding-left: 48px;
    background: rgba(247, 118, 142, 0.06);
    border-left: 2px solid #f7768e;
    border-radius: 0 6px 6px 0;
    animation: msg-enter 0.2s ease-out;
  }

  @keyframes msg-enter {
    from { opacity: 0; transform: translateY(-4px); }
    to   { opacity: 1; transform: translateY(0); }
  }

  .input-edit-wrap {
    display: inline-flex;
    align-items: center;
    gap: 2px;
    animation: edit-pop 0.2s ease-out;
  }
  @keyframes edit-pop {
    from { opacity: 0; transform: scale(0.9); }
    to   { opacity: 1; transform: scale(1); }
  }
  .input-name-edit {
    width: 80px;
    padding: 3px 8px;
    font-size: 11px;
    font-family: inherit;
    border: 1px solid rgba(122, 162, 247, 0.5);
    border-radius: 6px;
    background: rgba(255, 255, 255, 0.05);
    color: #e1e4f0;
    outline: none;
    transition: box-shadow 0.2s, border-color 0.2s;
  }
  .input-name-edit:focus {
    box-shadow: 0 0 0 3px rgba(122,162,247,0.15);
    border-color: #7aa2f7;
  }
  .edit-action-btn {
    padding: 3px 6px;
    font-size: 11px;
    background: transparent;
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 6px;
    color: #7aa2f7;
    cursor: pointer;
    font-family: inherit;
    line-height: 1;
    transition: background 0.15s;
  }
  .edit-action-btn:hover { background: rgba(255, 255, 255, 0.06); }

  /* ── Context menu ── */
  .ctx-menu {
    position: fixed;
    z-index: 9999;
    background: rgba(36, 40, 59, 0.96);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 10px;
    padding: 4px;
    min-width: 160px;
    box-shadow: 0 10px 40px rgba(0,0,0,0.5);
    backdrop-filter: blur(30px);
    -webkit-backdrop-filter: blur(30px);
    animation: ctx-enter 0.12s ease-out;
  }
  @keyframes ctx-enter {
    from { opacity: 0; transform: scale(0.96) translateY(-2px); }
    to   { opacity: 1; transform: scale(1) translateY(0); }
  }
  .ctx-item {
    display: block;
    width: 100%;
    padding: 7px 12px;
    border: none;
    border-radius: 6px;
    background: transparent;
    color: rgba(192, 202, 245, 0.9);
    font-size: 12px;
    font-family: inherit;
    text-align: left;
    cursor: pointer;
    transition: background 0.1s;
  }
  .ctx-item:hover { background: rgba(122,162,247,0.15); color: #e1e4f0; }
  .ctx-item.danger:hover { background: rgba(247,118,142,0.12); color: #f7768e; }
  .ctx-sep {
    height: 1px;
    margin: 4px 6px;
    background: rgba(255, 255, 255, 0.08);
  }

  /* ── Light theme ── */
  :global(html.light) .card {
    background: rgba(0, 0, 0, 0.03);
    border-color: rgba(0, 0, 0, 0.06);
  }
  :global(html.light) .card:hover {
    background: rgba(0, 0, 0, 0.05);
    border-color: rgba(0, 0, 0, 0.1);
  }
  :global(html.light) .icon-wrap {
    background: rgba(52, 84, 138, 0.1);
    color: #34548a;
  }
  :global(html.light) h2 { color: #1a1b2e; }
  :global(html.light) .size-badge {
    background: rgba(52, 84, 138, 0.12);
    color: #34548a;
  }
  :global(html.light) .subtitle { color: rgba(52, 59, 88, 0.55); }
  :global(html.light) .label { color: rgba(52, 59, 88, 0.5); }
  :global(html.light) .value { color: rgba(52, 59, 88, 0.85); }
  :global(html.light) .accent { color: #34548a; }
  :global(html.light) .hdr { color: #8f5e15; }
  :global(html.light) .val { color: rgba(52, 59, 88, 0.7); }
  :global(html.light) .brightness {
    background: rgba(0, 0, 0, 0.03);
  }
  :global(html.light) .brightness input[type="range"] {
    background: rgba(0, 0, 0, 0.08);
  }
  :global(html.light) .brightness input[type="range"]::-webkit-slider-thumb {
    background: #34548a;
  }
  :global(html.light) .btn-group button {
    background: rgba(0, 0, 0, 0.04);
    border-color: rgba(0, 0, 0, 0.08);
    color: rgba(52, 59, 88, 0.8);
  }
  :global(html.light) .btn-group button:hover {
    background: rgba(0, 0, 0, 0.07);
    border-color: rgba(52, 84, 138, 0.3);
    color: #1a1b2e;
  }
  :global(html.light) .btn-group button.active {
    background: rgba(52, 84, 138, 0.12);
    border-color: rgba(52, 84, 138, 0.3);
    color: #34548a;
  }
  :global(html.light) .status-msg {
    color: #8c4351;
    background: rgba(140, 67, 81, 0.06);
    border-left-color: #8c4351;
  }
  :global(html.light) .input-name-edit {
    background: rgba(255, 255, 255, 0.8);
    color: #1a1b2e;
    border-color: #34548a;
  }
  :global(html.light) .edit-action-btn { color: #34548a; }
  :global(html.light) .edit-action-btn:hover { background: rgba(0, 0, 0, 0.05); }
  :global(html.light) .ctx-menu {
    background: rgba(213, 214, 219, 0.96);
    border-color: rgba(0, 0, 0, 0.1);
    box-shadow: 0 10px 40px rgba(0,0,0,0.12);
  }
  :global(html.light) .ctx-item { color: #343b58; }
  :global(html.light) .ctx-item:hover { background: rgba(52,84,138,0.1); color: #1a1b2e; }
  :global(html.light) .ctx-item.danger:hover { background: rgba(140,67,81,0.1); color: #8c4351; }
  :global(html.light) .ctx-sep { background: rgba(0, 0, 0, 0.08); }
</style>
